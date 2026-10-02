// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for std::map / std::unordered_map / std::optional.
//
// Both used to fall into the class branch of type_descriptor as kind
// "object", which each backend then read its own way: pybind and nanobind
// bound them through their STL casters (a std:: exemption in the class gate),
// rest / openapi / wasm / lua / julia skipped them as unbound classes, and node
// took a std::map PARAMETER by unwrapping it as a wrapped object.
//
// What is pinned here:
//
//   * the IR marks them (is_map / is_optional) with kind "unknown", so every
//     backend that does not opt in skips them instead of guessing;
//   * a map key must be a string, number or enum, and neither wrapper may hold
//     a raw pointer or a callback;
//   * python / nanobind bind them through the STL casters, and an empty
//     optional default (`= std::nullopt`, `= {}`) becomes `= py::none()`;
//   * node binds them by copy (plain object / value-or-undefined), but only
//     when everything inside is a plain value;
//   * wasm puts a map on the emval wire as a plain object and registers each
//     wrapper type (register_type / register_optional);
//   * lua treats a map like a vector (userdata out, a plain table in through
//     sol::nested) and an optional through sol2's own nil-or-value;
//   * typescript declares `Record<K, V>` and `T | undefined`.
//
// Verifies the IR and the generated sources (render), not a live build.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ---- fixtures ---------------------------------------------------------------

namespace swx {

    enum class Color { Red, Green };

    struct Item {
        int id = 0;
    };

    struct Store {
        std::map<std::string, double>              weights;
        std::optional<int>                         limit;
        std::unordered_map<int, std::vector<int>>  groups;

        std::map<std::string, int>  counts() const { return {}; }
        void                        set_counts(const std::map<std::string, int> &) {}
        std::optional<double>       find(const std::string &, std::optional<int> hint = std::nullopt) {
            return hint ? std::optional<double>(*hint) : std::nullopt;
        }
        std::map<Color, std::string>              by_color() const { return {}; }
        std::vector<std::optional<int>>           sparse() const { return {}; }

        // A bound class inside a wrapper: python's caster handles it, node's
        // plain-value rule keeps it out.
        std::optional<Item>                       first() const { return std::nullopt; }
        std::map<std::string, Item>               items() const { return {}; }

        // Outside the shared rule on every backend: a class key, a pointer value.
        std::map<Item *, int>                     by_ptr() const { return {}; }
        std::optional<Item *>                     maybe_ptr() const { return std::nullopt; }
    };

} // namespace swx

template <> struct rosetta::binding_info<swx::Item> {
    static constexpr const char *header = "swx.h";
};
template <> struct rosetta::binding_info<swx::Store> {
    static constexpr const char *header = "swx.h";
};

namespace {

    rosetta::GenContext full_context() {
        return rosetta::gen_detail::make_context<swx::Item, swx::Store>("swxtest");
    }

    std::string render(const char *lang, const rosetta::GenContext &c) {
        return rosetta::backend_registry().at(lang)->render(c);
    }

    bool has(const std::string &hay, const std::string &needle) {
        return hay.find(needle) != std::string::npos;
    }

    // typescript writes its .d.ts in emit(), so it is read back from disk.
    std::string typescript_dts() {
        namespace fs       = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "rosetta_std_wrappers_ts";
        fs::remove_all(dir);
        auto c    = full_context();
        c.out_dir = dir;
        rosetta::backend_registry().at("typescript")->emit(c);
        std::ifstream     in(dir / "typescript" / "swxtest.d.ts");
        std::stringstream ss;
        ss << in.rdbuf();
        fs::remove_all(dir);
        return ss.str();
    }

} // namespace

// ---- the IR -----------------------------------------------------------------

TEST(StdWrappers, MapIsFlaggedWithKeyAndValue) {
    const auto t = rosetta::gen_detail::type_descriptor<std::map<std::string, double>>();
    EXPECT_EQ(t.kind, "unknown"); // not "object": no backend may read it as a class
    EXPECT_TRUE(t.is_map);
    EXPECT_FALSE(t.is_optional);
    ASSERT_EQ(t.element.size(), 2u);
    EXPECT_EQ(t.element[0].kind, "string");
    EXPECT_EQ(t.element[1].kind, "number");
    EXPECT_TRUE(rosetta::gen_detail::std_wrapper_ok(t));
}

TEST(StdWrappers, UnorderedMapIsAMapToo) {
    const auto t =
        rosetta::gen_detail::type_descriptor<std::unordered_map<int, std::vector<int>>>();
    EXPECT_TRUE(t.is_map);
    ASSERT_EQ(t.element.size(), 2u);
    EXPECT_EQ(t.element[1].kind, "vector");
}

TEST(StdWrappers, OptionalIsFlaggedWithItsValue) {
    const auto t = rosetta::gen_detail::type_descriptor<std::optional<int>>();
    EXPECT_EQ(t.kind, "unknown");
    EXPECT_TRUE(t.is_optional);
    ASSERT_EQ(t.element.size(), 1u);
    EXPECT_EQ(t.element[0].kind, "number");
    EXPECT_TRUE(t.element[0].integer);
}

TEST(StdWrappers, SharedRuleRefusesClassKeysAndPointers) {
    using rosetta::gen_detail::std_wrapper_ok;
    using rosetta::gen_detail::type_descriptor;
    EXPECT_TRUE(std_wrapper_ok(type_descriptor<std::map<swx::Color, int>>()));
    EXPECT_FALSE(std_wrapper_ok(type_descriptor<std::map<swx::Item, int>>()));
    EXPECT_FALSE(std_wrapper_ok(type_descriptor<std::map<std::string, swx::Item *>>()));
    EXPECT_FALSE(std_wrapper_ok(type_descriptor<std::optional<swx::Item *>>()));
    EXPECT_FALSE(std_wrapper_ok(type_descriptor<std::optional<std::function<void()>>>()));
    EXPECT_FALSE(std_wrapper_ok(type_descriptor<std::vector<int>>())); // not a wrapper
}

// ---- python / nanobind ------------------------------------------------------

TEST(StdWrappers, PythonFamilyBindsThroughTheStlCasters) {
    for (const char *lang : {"python", "nanobind"}) {
        const std::string s = render(lang, full_context());
        for (const char *m : {"\"counts\"", "\"set_counts\"", "\"find\"", "\"by_color\"",
                              "\"sparse\"", "\"first\"", "\"items\""}) {
            EXPECT_TRUE(has(s, m)) << lang << " " << m;
        }
        for (const char *f : {"\"weights\"", "\"limit\"", "\"groups\""}) {
            EXPECT_TRUE(has(s, f)) << lang << " " << f;
        }
        EXPECT_FALSE(has(s, "\"by_ptr\"")) << lang;
        EXPECT_FALSE(has(s, "\"maybe_ptr\"")) << lang;
    }
}

// The default's spelling is harvested from the header text by rosetta_gen, so
// it is absent from an in-memory context: feed px_arg_list the IR it would get.
TEST(StdWrappers, EmptyOptionalDefaultIsNone) {
    const auto      c = full_context();
    rosetta::GenParam name;
    name.name = "name";
    name.type = rosetta::gen_detail::type_descriptor<std::string>();
    rosetta::GenParam hint;
    hint.name         = "hint";
    hint.type         = rosetta::gen_detail::type_descriptor<std::optional<int>>();
    hint.has_default  = true;
    for (const char *d : {"std::nullopt", "{}"}) {
        hint.default_text = d;
        EXPECT_EQ(rosetta::backend::px_arg_list({name, hint}, "py", c),
                  ", py::arg(\"name\"), py::arg(\"hint\") = py::none()")
            << d;
        EXPECT_EQ(rosetta::backend::px_arg_list({name, hint}, "nb", c),
                  ", nb::arg(\"name\"), nb::arg(\"hint\") = nb::none()")
            << d;
    }
    // A non-empty optional default is not ours to translate: no default at all.
    hint.default_text = "3";
    EXPECT_EQ(rosetta::backend::px_arg_list({name, hint}, "py", c),
              ", py::arg(\"name\"), py::arg(\"hint\")");
}

// ---- node -------------------------------------------------------------------

TEST(StdWrappers, NodeBindsPlainValueWrappers) {
    const std::string s = render("node", full_context());
    for (const char *m : {"\"counts\"", "\"set_counts\"", "\"find\"", "\"by_color\"",
                          "\"sparse\"", "\"weights\"", "\"limit\"", "\"groups\""}) {
        EXPECT_TRUE(has(s, m)) << m;
    }
}

TEST(StdWrappers, NodeKeepsClassesAndPointersOut) {
    const std::string s = render("node", full_context());
    for (const char *m : {"\"first\"", "\"items\"", "\"by_ptr\"", "\"maybe_ptr\""}) {
        EXPECT_FALSE(has(s, m)) << m;
    }
}

// ---- wasm -------------------------------------------------------------------

TEST(StdWrappers, WasmBindsAndRegistersEachWrapper) {
    const std::string s = render("wasm", full_context());
    // A class inside a wrapper is fine here: embind copies it in and out.
    for (const char *m : {"\"counts\"", "\"set_counts\"", "\"find\"", "\"by_color\"",
                          "\"sparse\"", "\"first\"", "\"items\"", "\"weights\"", "\"limit\"",
                          "\"groups\""}) {
        EXPECT_TRUE(has(s, m)) << m;
    }
    EXPECT_FALSE(has(s, "\"by_ptr\""));
    EXPECT_FALSE(has(s, "\"maybe_ptr\""));
    // A map is an emval passthrough under its EXACT type (comparator and
    // allocator included: that is the type id embind checks) ...
    EXPECT_TRUE(has(s, "emscripten::register_type<std::map<std::string, int, "
                       "std::less<std::string>,"));
    EXPECT_TRUE(has(s, "(\"Record<string, number>\");"));
    // ... an optional uses embind's own registration ...
    EXPECT_TRUE(has(s, "emscripten::register_optional<int>();"));
    EXPECT_TRUE(has(s, "emscripten::register_optional<double>();"));
    // ... a vector reached only through a map still registers ...
    EXPECT_TRUE(has(s, "emscripten::register_type<std::vector<int>>"));
    // ... and a wrapper that cannot cross registers nothing.
    EXPECT_FALSE(has(s, "register_optional<swx::Item *>"));
    EXPECT_FALSE(has(s, "register_optional<Item *>"));
    EXPECT_TRUE(has(s, "struct BindingType<std::map<K, V, C, A>>"));
}

// ---- lua --------------------------------------------------------------------

TEST(StdWrappers, LuaBindsMapsLikeVectorsAndOptionalsNatively) {
    const std::string s = render("lua", full_context());
    for (const char *m : {"\"counts\"", "\"set_counts\"", "\"find\"", "\"by_color\"",
                          "\"first\"", "\"items\"", "\"weights\"", "\"limit\"",
                          "\"groups\""}) {
        EXPECT_TRUE(has(s, m)) << m;
    }
    EXPECT_FALSE(has(s, "\"by_ptr\""));
    EXPECT_FALSE(has(s, "\"maybe_ptr\""));
    // A map parameter gets the plain-table overload a vector gets.
    EXPECT_TRUE(has(s, "sol::nested<std::map<std::string, int"));
}

TEST(StdWrappers, LuaKeepsContainersOutOfOptionals) {
    // sol2 would read a failed table conversion as an EMPTY optional.
    rosetta::GenContext c;
    EXPECT_FALSE(rosetta::backend::lx_marshalable(
        rosetta::gen_detail::type_descriptor<std::optional<std::vector<int>>>(), c));
    EXPECT_TRUE(rosetta::backend::lx_marshalable(
        rosetta::gen_detail::type_descriptor<std::optional<std::string>>(), c));
}

// ---- a backend that did not opt in -------------------------------------------

TEST(StdWrappers, NonOptedBackendsSkip) {
    const std::string s = render("julia", full_context());
    EXPECT_FALSE(has(s, "\"counts\""));
    EXPECT_FALSE(has(s, "\"find\""));
    EXPECT_TRUE(has(s, "Store")); // the class itself still binds
}

// ---- typescript -------------------------------------------------------------

TEST(StdWrappers, TypescriptDeclaresRecordAndUndefined) {
    const std::string s = typescript_dts();
    ASSERT_FALSE(s.empty());
    EXPECT_TRUE(has(s, "counts(): Record<string, number>;"));
    EXPECT_TRUE(has(s, "by_color(): Record<number, Color>") ||
                has(s, "by_color(): Record<number, string>"));
    EXPECT_TRUE(has(s, "sparse(): (number | undefined)[];"));
    EXPECT_TRUE(has(s, "number | undefined"));
}
