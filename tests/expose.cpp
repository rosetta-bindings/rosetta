// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for the manifest "expose" rename on FREE FUNCTIONS and
// EXTENSION METHODS.
//
// A free function binds under one module-level name — its "expose" override,
// or its reflected identifier — so two functions sharing an unqualified name
// (arch::solve and arch::sinv::solve) can coexist. make_function() puts the
// override in GenFunction::name and leaves `qualified` (the C++ spelling)
// alone, so EVERY backend picks the rename up: they all emit the name as a
// label and the qualified spelling for the function pointer.
//
// Class renaming (binding_info<T>::expose) is exercised by the backends that
// consume it; this file covers the function/extension half.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

namespace expns {
    struct Vec {
        double x = 0;
        double y = 0;
    };

    inline double norm(const Vec &v) { return v.x * v.x + v.y * v.y; }

    // An extension method: first parameter is the receiver.
    inline double scaled(Vec &v, double k) { return (v.x + v.y) * k; }
} // namespace expns

template <> struct rosetta::binding_info<expns::Vec> {
    static constexpr const char *header = "exp.h";
};

// ---- make_function ----------------------------------------------------------

TEST(Expose, OverridesTheBindingNameOnly) {
    const auto f =
        rosetta::make_function<^^expns::norm>("expns::norm", "exp.h", "the doc", "length");
    EXPECT_EQ(f.name, "length");            // what scripts see
    EXPECT_EQ(f.qualified, "expns::norm");  // what C++ spells
    EXPECT_EQ(f.header, "exp.h");
    EXPECT_EQ(f.doc, "the doc");
}

TEST(Expose, EmptyOrAbsentFallsBackToTheReflectedIdentifier) {
    const auto absent = rosetta::make_function<^^expns::norm>("expns::norm", "exp.h", "");
    EXPECT_EQ(absent.name, "norm");
    const auto empty = rosetta::make_function<^^expns::norm>("expns::norm", "exp.h", "", "");
    EXPECT_EQ(empty.name, "norm");
}

// ---- the rename reaches the backends ---------------------------------------

static rosetta::GenContext ctx_with_function(const char *expose) {
    auto c = rosetta::gen_detail::make_context<expns::Vec>("exptest");
    c.functions.push_back(
        rosetta::make_function<^^expns::norm>("expns::norm", "exp.h", "the doc", expose));
    return c;
}

static std::string render(const char *lang, const char *expose) {
    return rosetta::backend_registry().at(lang)->render(ctx_with_function(expose));
}

TEST(Expose, PythonBindsTheExposedNameToTheQualifiedFunction) {
    const std::string s = render("python-expanded", "length");
    EXPECT_NE(s.find("m.def(\"length\", &expns::norm"), std::string::npos);
    EXPECT_EQ(s.find("m.def(\"norm\""), std::string::npos);
}

TEST(Expose, NodeBindsTheExposedName) {
    const std::string s = render("node-expanded", "length");
    EXPECT_NE(s.find("exports.Set(\"length\""), std::string::npos);
    EXPECT_NE(s.find("&expns::norm"), std::string::npos);
    EXPECT_EQ(s.find("exports.Set(\"norm\""), std::string::npos);
}

TEST(Expose, NanobindBindsTheExposedName) {
    const std::string s = render("nanobind-expanded", "length");
    EXPECT_NE(s.find("m.def(\"length\", &expns::norm"), std::string::npos);
}

TEST(Expose, WasmBindsTheExposedName) {
    const std::string s = render("wasm-expanded", "length");
    EXPECT_NE(s.find("emscripten::function(\"length\", &expns::norm"), std::string::npos);
}

TEST(Expose, LuaBindsTheExposedName) {
    const std::string s = render("lua-expanded", "length");
    EXPECT_NE(s.find("m.set_function(\"length\", &expns::norm"), std::string::npos);
}

TEST(Expose, MarkdownDocumentsTheExposedName) {
    const std::string s = render("markdown", "length");
    EXPECT_NE(s.find("`length("), std::string::npos);
    EXPECT_EQ(s.find("`norm("), std::string::npos);
}

TEST(Expose, WithoutOverrideEveryBackendKeepsTheIdentifier) {
    EXPECT_NE(render("python-expanded", "").find("m.def(\"norm\", &expns::norm"),
              std::string::npos);
    EXPECT_NE(render("node-expanded", "").find("exports.Set(\"norm\""), std::string::npos);
    EXPECT_NE(render("lua-expanded", "").find("m.set_function(\"norm\", &expns::norm"),
              std::string::npos);
}

// ---- extension methods ------------------------------------------------------
// generate() splices an extension into the class IR as a GenMethod whose
// `name` is the (possibly renamed) GenFunction name and whose call goes
// through ext_qualified — so the rename applies to methods too.

static rosetta::GenContext ctx_with_extension(const char *exposed_name) {
    auto             c = rosetta::gen_detail::make_context<expns::Vec>("exptest");
    rosetta::GenMethod m;
    m.name          = exposed_name;
    m.ret.kind      = "number";
    m.ret.spelling  = "double";
    m.ret_cpp       = "double";
    rosetta::GenParam p;
    p.name          = "k";
    p.type.kind     = "number";
    p.type.spelling = "double";
    m.params.push_back(p);
    m.param_cpp     = {"double"};
    m.is_extension  = true;
    m.ext_qualified = "expns::scaled";
    m.ext_header    = "exp.h";
    c.classes.front().methods.push_back(std::move(m));
    return c;
}

TEST(Expose, RenamedExtensionBindsUnderTheNewNameAndCallsTheFreeFunction) {
    const auto c = ctx_with_extension("stretch");
    const std::string py = rosetta::backend_registry().at("python-expanded")->render(c);
    EXPECT_NE(py.find("c.def(\"stretch\", &expns::scaled"), std::string::npos);
    EXPECT_EQ(py.find("&expns::Vec::stretch"), std::string::npos); // never a member pointer

    const std::string wasm = rosetta::backend_registry().at("wasm-expanded")->render(c);
    EXPECT_NE(wasm.find(".function(\"stretch\", &expns::scaled)"), std::string::npos);
}

// ---- class / enum rename across the backends --------------------------------
// Every backend names the bound type by its "expose" override in the host
// language, while every C++ spelling it emits stays qualified.

namespace rnns {
    enum class Kind { A = 0, B = 1 };

    struct Thing {
        double      value = 0;
        Kind        kind  = Kind::A;
        double      twice() const { return value * 2; }
        static Kind best() { return Kind::B; }
    };
} // namespace rnns

template <> struct rosetta::binding_info<rnns::Thing> {
    static constexpr const char *header = "rn.h";
    static constexpr const char *expose = "Widget";
};
template <> struct rosetta::binding_info<rnns::Kind> {
    static constexpr const char *header = "rn.h";
    static constexpr const char *expose = "Flavor";
};

static std::string renamed(const char *lang) {
    const auto c = rosetta::gen_detail::make_context<rnns::Thing, rnns::Kind>("rntest");
    return rosetta::backend_registry().at(lang)->render(c);
}

// The IR carries both names, for the class and the enumeration alike.
TEST(Expose, IrCarriesBothNamesForClassesAndEnums) {
    const auto c = rosetta::gen_detail::make_context<rnns::Thing, rnns::Kind>("rntest");
    ASSERT_EQ(c.classes.size(), 1u);
    ASSERT_EQ(c.enums.size(), 1u);
    EXPECT_EQ(c.classes.front().name, "Thing");
    EXPECT_EQ(rosetta::gen_detail::exposed_of(c.classes.front()), "Widget");
    EXPECT_EQ(rosetta::gen_detail::qualified_of(c.classes.front()), "rnns::Thing");
    EXPECT_EQ(c.enums.front().name, "Kind");
    EXPECT_EQ(rosetta::gen_detail::exposed_of(c.enums.front()), "Flavor");
    EXPECT_EQ(rosetta::gen_detail::qualified_of(c.enums.front()), "rnns::Kind");
}

TEST(Expose, EveryRenderingBackendUsesTheExposedName) {
    // Each entry: backend, a snippet that must appear (the exposed name in the
    // host language), and one that must NOT (the raw C++ identifier as a name).
    struct Case {
        const char *lang;
        const char *wants;
        const char *rejects;
    };
    const Case cases[] = {
        {"python", "\"Widget\"", "\"Thing\""},
        {"python-expanded", "\"Widget\"", "\"Thing\""},
        {"nanobind", "\"Widget\"", "\"Thing\""},
        {"nanobind-expanded", "nb::class_<rnns::Thing>(m, \"Widget\")", "(m, \"Thing\")"},
        {"node", "\"Widget\"", "\"Thing\""},
        {"node-expanded", "\"Widget\"", "\"Thing\""},
        {"wasm-expanded", "emscripten::class_<rnns::Thing>(\"Widget\")", "(\"Thing\")"},
        {"lua-expanded", "m.new_usertype<rnns::Thing>(\"Widget\"", "(\"Thing\""},
        {"julia-expanded", "mod.add_type<rnns::Thing>(\"Widget\"", "(\"Thing\""},
        {"csharp", "rosetta::bind_csharp<rnns::Thing>(\"Widget\")", "(\"Thing\")"},
        {"csharp-expanded", "registry()[\"Widget\"]", "registry()[\"Thing\"]"},
        {"java", "rosetta::bind_java<rnns::Thing>(\"Widget\")", "(\"Thing\")"},
        {"java-expanded", "registry()[\"Widget\"]", "registry()[\"Thing\"]"},
        {"markdown", "Widget", "# Thing"},
        {"html", "Widget", "<h2>Thing</h2>"},
        {"paraview", "name=\"Widget\"", "name=\"Thing\""},
    };
    for (const auto &t : cases) {
        const std::string s = renamed(t.lang);
        EXPECT_NE(s.find(t.wants), std::string::npos) << t.lang << " lacks " << t.wants;
        EXPECT_EQ(s.find(t.rejects), std::string::npos) << t.lang << " still emits " << t.rejects;
    }
}

TEST(Expose, RenamedEnumKeepsTheQualifiedCppSpelling) {
    // The label is the exposed name; the C++ template argument / enumerator
    // spelling stays qualified, which is what disambiguates two bound enums.
    const std::string py = renamed("python-expanded");
    EXPECT_NE(py.find("py::enum_<rnns::Kind>(m, \"Flavor\")"), std::string::npos);
    EXPECT_NE(py.find(".value(\"A\", rnns::Kind::A)"), std::string::npos);

    const std::string lua = renamed("lua-expanded");
    EXPECT_NE(lua.find("m.new_enum<rnns::Kind>(\"Flavor\""), std::string::npos);

    // The C# / Java wrappers are separate artifacts from what render() returns.
    const auto        c  = rosetta::gen_detail::make_context<rnns::Thing, rnns::Kind>("rntest");
    const std::string cs = rosetta::gen_detail::csharp_wrapper(c);
    EXPECT_NE(cs.find("public enum Flavor"), std::string::npos);
    EXPECT_NE(cs.find("public sealed class Widget"), std::string::npos);
    EXPECT_EQ(cs.find("class Thing"), std::string::npos);
    // A field of the renamed enum type is declared with the renamed type.
    EXPECT_NE(cs.find("Flavor kind"), std::string::npos);

    const std::string jv = rosetta::gen_detail::java_class(c, c.classes.front());
    EXPECT_NE(jv.find("public final class Widget"), std::string::npos);
    EXPECT_NE(jv.find("_t = \"Widget\""), std::string::npos);
    const std::string je = rosetta::gen_detail::java_enum(c, c.enums.front());
    EXPECT_NE(je.find("public enum Flavor"), std::string::npos);
}
