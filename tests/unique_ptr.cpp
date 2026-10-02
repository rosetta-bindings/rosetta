// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for std::unique_ptr.
//
// A factory returning std::unique_ptr<T> used to be skipped by every backend:
// the IR described it as a class named "unique_ptr<T, default_delete<T>>",
// which nobody binds and which cannot be copied. Worse, that same description
// let pybind and node bind a `const std::unique_ptr<T>&` PARAMETER — pybind
// throwing on every call, node reinterpreting the JS wrapper as a unique_ptr
// and crashing.
//
// What is pinned here:
//
//   * the IR marks it (is_unique_ptr, kind "unknown", element = the pointee),
//     so a backend that does not opt in skips every unique_ptr;
//   * the opted-in backends bind exactly one shape — a BY-VALUE return whose
//     pointee is bound — and the host object owns the pointee afterwards;
//   * parameters, fields and reference returns stay out everywhere;
//   * python converts to std::shared_ptr only for a pointee registered with a
//     shared_ptr holder (pybind crashes on a unique_ptr return there), and
//     returns natively otherwise — no class's holder changes;
//   * node adopts it through the shared_ptr path, so it keeps that path's
//     no-virtuals rule.
//
// Verifies the IR and the generated sources (render), not a live build — the
// last two cases were found by building that output.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

// ---- fixtures ---------------------------------------------------------------

namespace upx {

    struct Doc {
        int n = 7;
        int size() const { return n; }
    };

    // With virtuals: node's adopting Wrap cannot hold it.
    struct Shape {
        virtual ~Shape() = default;
        virtual int                    sides() const { return 3; }
        virtual std::unique_ptr<Shape> clone() const { return std::make_unique<Shape>(*this); }
    };

    // Travels as a shared_ptr too, so pybind registers it with a shared holder.
    struct Both {
        int n = 2;
    };

    struct Unbound {};

    struct Factory {
        std::unique_ptr<Doc> owned = std::make_unique<Doc>();

        std::unique_ptr<Doc>        make() const { return std::make_unique<Doc>(); }
        static std::unique_ptr<Doc> create(int n) {
            auto d = std::make_unique<Doc>();
            d->n   = n;
            return d;
        }
        std::unique_ptr<Shape>  make_shape() const { return std::make_unique<Shape>(); }
        std::shared_ptr<Both>   share() const { return std::make_shared<Both>(); }
        std::unique_ptr<Both>   make_both() const { return std::make_unique<Both>(); }
        std::unique_ptr<Unbound> stray() const { return {}; }

        int                         consume(std::unique_ptr<Doc> d) const { return d ? d->n : -1; }
        int                         peek(const std::unique_ptr<Doc> &d) const { return d ? d->n : -1; }
        const std::unique_ptr<Doc> &held() const { return owned; }
    };

    inline std::unique_ptr<Doc>        make_doc(int n) { return Factory::create(n); }
    inline const std::unique_ptr<Doc> &doc_ref() {
        static std::unique_ptr<Doc> d = std::make_unique<Doc>();
        return d;
    }

} // namespace upx

template <> struct rosetta::binding_info<upx::Doc> {
    static constexpr const char *header = "upx.h";
};
template <> struct rosetta::binding_info<upx::Shape> {
    static constexpr const char *header = "upx.h";
};
template <> struct rosetta::binding_info<upx::Both> {
    static constexpr const char *header = "upx.h";
};
template <> struct rosetta::binding_info<upx::Factory> {
    static constexpr const char *header = "upx.h";
};

namespace {

    rosetta::GenContext full_context() {
        auto c = rosetta::gen_detail::make_context<upx::Doc, upx::Shape, upx::Both, upx::Factory>(
            "upxtest");
        c.functions.push_back(
            rosetta::make_function<^^upx::make_doc>("upx::make_doc", "upx.h", "", ""));
        c.functions.push_back(
            rosetta::make_function<^^upx::doc_ref>("upx::doc_ref", "upx.h", "", ""));
        return c;
    }

    std::string render(const char *lang) {
        return rosetta::backend_registry().at(lang)->render(full_context());
    }

    bool has(const std::string &hay, const std::string &needle) {
        return hay.find(needle) != std::string::npos;
    }

    bool binds(const std::string &src, const char *name) {
        return has(src, std::string("\"") + name + "\"");
    }

    // Never bound anywhere: ownership the script would have to give up, a
    // reference that hands over none, a member the object keeps, a pointee no
    // one registered.
    void expect_refused(const std::string &s, const char *lang) {
        for (const char *m : {"consume", "peek", "held", "owned", "stray", "doc_ref"}) {
            EXPECT_FALSE(binds(s, m)) << lang << " " << m;
        }
    }

} // namespace

// ---- the IR -----------------------------------------------------------------

TEST(UniquePtr, FlaggedWithItsPointee) {
    const auto t = rosetta::gen_detail::type_descriptor<std::unique_ptr<upx::Doc>>();
    EXPECT_EQ(t.kind, "unknown"); // not "object": no backend may read it as a class
    EXPECT_TRUE(t.is_unique_ptr);
    ASSERT_EQ(t.element.size(), 1u);
    EXPECT_EQ(t.element[0].kind, "object");
    EXPECT_EQ(t.element[0].object_qualified, "upx::Doc");
    EXPECT_EQ(&rosetta::gen_detail::unique_pointee(t), &t.element[0]);
}

TEST(UniquePtr, CustomDeleterAndArrayAreNotFlagged) {
    using rosetta::gen_detail::type_descriptor;
    struct Del {
        void operator()(upx::Doc *p) const { delete p; }
    };
    EXPECT_FALSE((type_descriptor<std::unique_ptr<upx::Doc, Del>>().is_unique_ptr));
    EXPECT_FALSE(type_descriptor<std::unique_ptr<int[]>>().is_unique_ptr);
}

TEST(UniquePtr, OnlyAByValueReturnOfABoundClassQualifies) {
    using rosetta::gen_detail::type_descriptor;
    using rosetta::gen_detail::unique_return_ok;
    const auto c = full_context();
    EXPECT_TRUE(unique_return_ok(type_descriptor<std::unique_ptr<upx::Doc>>(), false, c));
    EXPECT_FALSE(unique_return_ok(type_descriptor<std::unique_ptr<upx::Doc>>(), true, c));
    EXPECT_FALSE(unique_return_ok(type_descriptor<std::unique_ptr<upx::Unbound>>(), false, c));
    EXPECT_FALSE(unique_return_ok(type_descriptor<std::unique_ptr<int>>(), false, c));
    EXPECT_FALSE(unique_return_ok(type_descriptor<std::shared_ptr<upx::Doc>>(), false, c));
}

TEST(UniquePtr, FreeFunctionsRecordAReferenceReturn) {
    const auto c = full_context();
    ASSERT_EQ(c.functions.size(), 2u);
    EXPECT_FALSE(c.functions[0].ret_is_ref); // make_doc
    EXPECT_TRUE(c.functions[1].ret_is_ref);  // doc_ref
}

// ---- python / nanobind ------------------------------------------------------

TEST(UniquePtr, PythonReturnsNativelyForTheDefaultHolder) {
    const std::string s = render("python");
    EXPECT_TRUE(has(s, "&upx::Factory::make)"));
    EXPECT_TRUE(binds(s, "create"));
    EXPECT_TRUE(binds(s, "make_shape"));
    EXPECT_TRUE(binds(s, "make_doc"));
    // Doc keeps the default holder: nothing about its registration changed.
    EXPECT_FALSE(has(s, "std::shared_ptr<upx::Doc>"));
    expect_refused(s, "python");
}

TEST(UniquePtr, PythonConvertsForASharedHolder) {
    const std::string s = render("python");
    EXPECT_TRUE(has(s, "py::class_<upx::Both, std::shared_ptr<upx::Both>>"));
    EXPECT_TRUE(has(s, "c.def(\"make_both\", [](const upx::Factory &self) { return "
                       "std::shared_ptr<upx::Both>(self.make_both()); }"));
}

TEST(UniquePtr, NanobindReturnsNatively) {
    const std::string s = render("nanobind");
    for (const char *m : {"make", "create", "make_shape", "make_both", "make_doc"}) {
        EXPECT_TRUE(binds(s, m)) << m;
    }
    EXPECT_FALSE(has(s, "std::shared_ptr<upx::Both>(self.make_both())"));
    expect_refused(s, "nanobind");
}

// ---- node -------------------------------------------------------------------

TEST(UniquePtr, NodeAdoptsWhenThePointeeHasNoVirtuals) {
    const std::string s = render("node");
    for (const char *m : {"make", "create", "make_both", "make_doc"}) {
        EXPECT_TRUE(binds(s, m)) << m;
    }
    EXPECT_FALSE(binds(s, "make_shape")); // Shape's wrapper is a trampoline
    expect_refused(s, "node");
}

// ---- wasm / lua ---------------------------------------------------------------

TEST(UniquePtr, WasmAndLuaReturnNatively) {
    for (const char *lang : {"wasm", "lua"}) {
        const std::string s = render(lang);
        for (const char *m : {"make", "create", "make_shape", "make_both", "make_doc"}) {
            EXPECT_TRUE(binds(s, m)) << lang << " " << m;
        }
        expect_refused(s, lang);
    }
}

// ---- a backend that did not opt in -------------------------------------------

TEST(UniquePtr, NonOptedBackendsSkip) {
    const std::string s = render("julia");
    EXPECT_FALSE(binds(s, "make"));
    EXPECT_FALSE(binds(s, "peek"));
}

// ---- typescript -------------------------------------------------------------

TEST(UniquePtr, TypescriptDeclaresThePointee) {
    namespace fs       = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "rosetta_unique_ptr_ts";
    fs::remove_all(dir);
    auto c    = full_context();
    c.out_dir = dir;
    rosetta::backend_registry().at("typescript")->emit(c);
    std::ifstream     in(dir / "typescript" / "upxtest.d.ts");
    std::stringstream ss;
    ss << in.rdbuf();
    fs::remove_all(dir);
    const std::string s = ss.str();
    ASSERT_FALSE(s.empty());
    EXPECT_TRUE(has(s, "make(): Doc;"));
    EXPECT_FALSE(has(s, "unique_ptr"));
}

// ---- found by building the output ---------------------------------------------

// A JS override cannot hand C++ ownership, and the override napi_call_override
// would need did not compile — which used to break the whole node module for
// any class declaring `virtual std::unique_ptr<T> clone() const`.
TEST(UniquePtr, NodeTrampolineSkipsAUniqueReturningVirtual) {
    const std::string s = render("node");
    ASSERT_TRUE(has(s, "class Js_Shape"));
    EXPECT_TRUE(has(s, "sides() const override"));
    EXPECT_FALSE(has(s, "clone() const override"));
}

// The reflected spelling is `unique_ptr<T, default_delete<T>>`; a backend
// whose generated code does not open namespace std (embind) needs both
// tokens qualified.
TEST(UniquePtr, QualifyStdKnowsDefaultDelete) {
    EXPECT_EQ(rosetta::backend::qualify_std("unique_ptr<upx::Doc, default_delete<upx::Doc>>"),
              "std::unique_ptr<upx::Doc, std::default_delete<upx::Doc>>");
}
