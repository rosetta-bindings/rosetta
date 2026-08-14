// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for OVERLOAD SELECTION ON FREE FUNCTIONS — the manifest's
// `"signature"` on a "functions" entry.
//
// The member-function overload work (overload.cpp) stops at class scope. At
// NAMESPACE scope nothing had changed: `^^GEO::mesh_union` is ill-formed when
// the name is an overload set, so the whole entry was unbindable and the only
// way out was a hand-written, differently-named wrapper. A signature names one
// member of the set without reflecting the function at all — the function TYPE
// is an ordinary type argument — and the backends form the pointer through a
// disambiguating static_cast.
//
// What is pinned here:
//
//   * make_function_sig fills the same IR make_function does (name, return,
//     parameters, ref/mutable-ref flags) plus sig_cpp;
//   * the exposed name falls back to the tail of the qualified spelling, since
//     there is no identifier_of to ask, and "expose" still wins;
//   * every backend that FORMS A POINTER emits the cast — including the ones
//     that spell it as a template argument (node's napi_free_entry);
//   * a backend that splices the function's reflection skips the entry, since
//     there is no reflection for one member of an overload set;
//   * a function with no signature keeps the byte-identical old output.
//
// Verifies the IR and the generated sources (render), not a live build.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ---- fixtures ---------------------------------------------------------------

namespace fov {
    // The overload set: same name, three signatures. `^^fov::scale` is
    // ill-formed — that is the whole point of the feature.
    void scale(int) {}
    void scale(int, int) {}
    int  scale(double d, bool) { return int(d); }

    // Not overloaded: keeps the reflection path, and pins that its emitted text
    // did not change.
    int plain(int a) { return a; }
} // namespace fov

namespace {

    // A context with the two functions the suite exercises: the selected
    // overload `void(int, int)` and the ordinary `fov::plain`.
    rosetta::GenContext ctx_with_functions() {
        rosetta::GenContext c = rosetta::gen_detail::make_context<>("fovtest");
        c.functions.push_back(rosetta::make_function_sig<void(int, int)>(
            "fov::scale", "fov.h", "two ints", "", "void(int, int)"));
        c.functions.push_back(rosetta::make_function<^^fov::plain>("fov::plain", "fov.h", "", ""));
        return c;
    }

    std::string render(const char *lang, const rosetta::GenContext &c) {
        return rosetta::backend_registry().at(lang)->render(c);
    }

    bool has(const std::string &hay, const std::string &needle) {
        return hay.find(needle) != std::string::npos;
    }

} // namespace

// ---- the IR -----------------------------------------------------------------

TEST(FreeOverload, SignatureFillsTheSameIrAsReflection) {
    const rosetta::GenFunction f = rosetta::make_function_sig<void(int, int)>(
        "fov::scale", "fov.h", "two ints", "", "void(int, int)");
    EXPECT_EQ(f.name, "scale"); // tail of the qualified spelling
    EXPECT_EQ(f.qualified, "fov::scale");
    EXPECT_EQ(f.header, "fov.h");
    EXPECT_EQ(f.doc, "two ints");
    EXPECT_EQ(f.ret.kind, "void");
    ASSERT_EQ(f.params.size(), 2u);
    EXPECT_EQ(f.params[0].name, "arg0");
    EXPECT_EQ(f.params[1].name, "arg1");
    EXPECT_EQ(f.params[0].type.kind, "number"); // the IR's language-neutral kind
    EXPECT_EQ(f.params[0].type.spelling, "int");
    EXPECT_EQ(f.sig_cpp, "void(int, int)");
}

// The ref / mutable-ref reading is what tells a backend an argument is an
// out-parameter; it must come out of a signature exactly as it does out of a
// reflection.
TEST(FreeOverload, ReferenceParametersKeepTheirFlags) {
    const rosetta::GenFunction f = rosetta::make_function_sig<void(int &, const double &, bool)>(
        "fov::refs", "fov.h", "", "", "void(int&, const double&, bool)");
    ASSERT_EQ(f.params.size(), 3u);
    EXPECT_TRUE(f.params[0].is_ref);
    EXPECT_TRUE(f.params[0].is_mutable_ref);
    EXPECT_TRUE(f.params[1].is_ref);
    EXPECT_FALSE(f.params[1].is_mutable_ref); // const& is not an out-parameter
    EXPECT_FALSE(f.params[2].is_ref);
}

TEST(FreeOverload, ExposeStillOverridesTheName) {
    const rosetta::GenFunction f = rosetta::make_function_sig<int(double, bool)>(
        "fov::scale", "fov.h", "", "scale_d", "int(double, bool)");
    EXPECT_EQ(f.name, "scale_d");
    EXPECT_EQ(f.qualified, "fov::scale"); // the C++ spelling is untouched
}

TEST(FreeOverload, NoSignatureLeavesSigCppEmpty) {
    const rosetta::GenFunction f =
        rosetta::make_function<^^fov::plain>("fov::plain", "fov.h", "", "");
    EXPECT_TRUE(f.sig_cpp.empty());
}

// ---- the cast ---------------------------------------------------------------

TEST(FreeOverload, FnAddrCastsOnlyWhenAnOverloadWasSelected) {
    const auto c = ctx_with_functions();
    EXPECT_EQ(rosetta::gen_detail::fn_addr(c.functions[0]),
              "static_cast<void(*)(int, int)>(&fov::scale)");
    EXPECT_EQ(rosetta::gen_detail::fn_addr(c.functions[1]), "&fov::plain");
}

// The pointer form is built by inserting `(*)` at the parameter list — which
// must be found OUTSIDE any template argument list, or a return type carrying
// its own parentheses inside `<>` would split in the wrong place.
TEST(FreeOverload, PointerSpellingSkipsTemplateArguments) {
    EXPECT_EQ(rosetta::gen_detail::fn_ptr_spelling("std::vector<int>(int, bool)"),
              "std::vector<int>(*)(int, bool)");
    EXPECT_EQ(rosetta::gen_detail::fn_ptr_spelling("std::function<void(int)>(double)"),
              "std::function<void(int)>(*)(double)");
}

// ---- the backends that form a pointer ---------------------------------------

TEST(FreeOverload, PythonEmitsTheCast) {
    const std::string s = render("python", ctx_with_functions());
    EXPECT_TRUE(has(s, "m.def(\"scale\", static_cast<void(*)(int, int)>(&fov::scale)"));
    EXPECT_TRUE(has(s, "m.def(\"plain\", &fov::plain")); // unchanged
}

TEST(FreeOverload, WasmEmitsTheCast) {
    const std::string s = render("wasm", ctx_with_functions());
    EXPECT_TRUE(has(s, "emscripten::function(\"scale\", static_cast<void(*)(int, int)>(&fov::scale)"));
}

TEST(FreeOverload, LuaExpandedEmitsTheCast) {
    const std::string s = render("lua-expanded", ctx_with_functions());
    EXPECT_TRUE(has(s, "m.set_function(\"scale\", static_cast<void(*)(int, int)>(&fov::scale)"));
}

// The interesting one: node spells the pointer as a TEMPLATE ARGUMENT
// (`napi_free_entry<&fn>`), where `&fov::scale` is just as ambiguous as it is in
// a function argument — a cast is a valid non-type template argument, `&`-of an
// overload set is not.
TEST(FreeOverload, NodeCastsInsideTheTemplateArgument) {
    const std::string s = render("node", ctx_with_functions());
    EXPECT_TRUE(has(
        s, "rosetta::napi_free_entry<static_cast<void(*)(int, int)>(&fov::scale)>"));
    EXPECT_TRUE(has(s, "rosetta::napi_free_entry<&fov::plain>"));
}

// C#, Java and Julia used to SPLICE the function's reflection (`^^fov::scale`),
// which has no spelling for one member of an overload set, so they skipped such
// an entry outright. Their reflection-driven halves are gone: all three now form
// the pointer like everyone else, and bind the selected overload.
TEST(FreeOverload, RegistryBackendsEmitTheCast) {
    for (const char *lang : {"csharp", "java"}) {
        const std::string s = render(lang, ctx_with_functions());
        EXPECT_TRUE(has(s, "static_cast<void(*)(int, int)>(&fov::scale)")) << lang;
        EXPECT_FALSE(has(s, "^^fov::scale")) << lang;
    }
}

// ---- the one backend that still splices a reflection ------------------------

// rest builds its bindings inside emit() rather than render(), so the guard has
// to be read off the generated file.
TEST(FreeOverload, TheReflectionSplicingBackendSkipsIt) {
    namespace fs = std::filesystem;
    for (const auto &[lang, file] : std::vector<std::pair<const char *, const char *>>{
             {"rest", "auto_rest.cpp"}}) {
        const fs::path dir =
            fs::temp_directory_path() / (std::string("rosetta_free_overload_") + lang);
        fs::remove_all(dir);
        auto c    = ctx_with_functions();
        c.out_dir = dir;
        rosetta::backend_registry().at(lang)->emit(c);

        std::string src;
        for (const auto &e : fs::recursive_directory_iterator(dir)) {
            if (e.path().filename() == file) {
                std::ifstream     in(e.path());
                std::stringstream ss;
                ss << in.rdbuf();
                src = ss.str();
                break;
            }
        }
        fs::remove_all(dir);
        ASSERT_FALSE(src.empty()) << lang;
        EXPECT_FALSE(has(src, "^^fov::scale")) << lang;
        EXPECT_TRUE(has(src, "^^fov::plain")) << lang;
    }
}
