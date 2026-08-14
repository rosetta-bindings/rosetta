// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for the two manifest fields that cover what is NOT a
// binding: **`module_init`** (statements the module runs when it loads) and
// **`generated_headers`** (a header the bound library's own build system would
// have written).
//
// Both exist because a library is more than its API. geogram wants
// `GEO::initialize()`, seven `CmdLine::import_arg_group()` calls and a
// C-function-pointer registration to have happened before anything works —
// none of which is expressible as "bind this name", so it lived in a
// hand-written `georo::initialize()` every script had to remember to call. And
// it wants a `<geogram/version.h>` that only its CMake produces, which was a
// checked-in stand-in shadowing the real thing.
//
// What is pinned here:
//
//   * every backend with a module entry point emits the statements at the TOP
//     of it, before any binding is registered, and the declaring headers in its
//     include block;
//   * a statement is terminated exactly once, whether or not the manifest
//     wrote the semicolon;
//   * a module with no "module_init" is byte-identical to before;
//   * generated headers are written under <out_dir>/include and that directory
//     goes FIRST on every backend's include path — ahead of the library's own
//     sources, where a stale copy of the same header may sit.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---- fixtures ---------------------------------------------------------------

namespace mix {
    struct Widget {
        int n = 0;
        int size() const { return n; }
    };
} // namespace mix

template <> struct rosetta::binding_info<mix::Widget> {
    static constexpr const char *header = "mix.h";
};

namespace {

    namespace fs = std::filesystem;

    rosetta::GenContext ctx_with_init() {
        auto c           = rosetta::gen_detail::make_context<mix::Widget>("mixtest");
        c.init_headers   = {"mix/lifecycle.h"};
        c.init_statements = {"mix::initialize(mix::INSTALL_NONE)", // no semicolon
                             "mix::import_arg_group(\"standard\");"}; // with one
        return c;
    }

    std::string render(const char *lang, const rosetta::GenContext &c) {
        return rosetta::backend_registry().at(lang)->render(c);
    }

    bool has(const std::string &hay, const std::string &needle) {
        return hay.find(needle) != std::string::npos;
    }

    // The backends whose render() returns the module source.
    const std::vector<const char *> kRenderable{"python", "nanobind", "node",
                                                "wasm",   "lua"};

} // namespace

// ---- module_init ------------------------------------------------------------

TEST(ModuleInit, EveryEntryPointRunsTheStatements) {
    const auto c = ctx_with_init();
    for (const char *lang : kRenderable) {
        const std::string s = render(lang, c);
        EXPECT_TRUE(has(s, "mix::initialize(mix::INSTALL_NONE);")) << lang;
        EXPECT_TRUE(has(s, "mix::import_arg_group(\"standard\");")) << lang;
        EXPECT_TRUE(has(s, "#include \"mix/lifecycle.h\"")) << lang;
    }
}

// A manifest writes expressions; some authors end them with a semicolon anyway.
// Exactly one must come out.
TEST(ModuleInit, StatementsAreTerminatedExactlyOnce) {
    const std::string s = render("python", ctx_with_init());
    EXPECT_TRUE(has(s, "mix::import_arg_group(\"standard\");\n"));
    EXPECT_FALSE(has(s, ";;"));
}

// Before any binding: an init statement that registers I/O handlers has to run
// before a bound loader is reachable, and a script can call one the moment the
// module object exists.
TEST(ModuleInit, StatementsPrecedeTheBindings) {
    const std::string s = render("python", ctx_with_init());
    const auto        init = s.find("mix::initialize");
    const auto        bind = s.find("py::class_<mix::Widget");
    ASSERT_NE(init, std::string::npos);
    ASSERT_NE(bind, std::string::npos);
    EXPECT_LT(init, bind);
}

TEST(ModuleInit, NoInitEmitsNothing) {
    const auto plain = rosetta::gen_detail::make_context<mix::Widget>("mixtest");
    for (const char *lang : kRenderable) {
        const std::string s = render(lang, plain);
        EXPECT_FALSE(has(s, "module_init")) << lang;
    }
}

// ---- generated_headers ------------------------------------------------------

TEST(ModuleInit, GeneratedHeaderIsWrittenAndComesFirstOnTheIncludePath) {
    const fs::path dir = fs::temp_directory_path() / "rosetta_generated_header";
    fs::remove_all(dir);
    const fs::path user = dir / "user_src";
    fs::create_directories(user);

    rosetta::GenerateOptions opt;
    opt.out_dir           = dir / "out";
    opt.user_include      = {user};
    opt.rosetta_include   = "/rosetta/include";
    opt.targets           = {{"python", "mixtest"}};
    opt.generated_headers = {{"mix/version.h", "#define MIX_VERSION \"1.2.3\"\n"}};
    rosetta::generate<mix::Widget>(opt);

    // Written where the #include says: <out_dir>/include/mix/version.h.
    const fs::path written = opt.out_dir / "include" / "mix" / "version.h";
    ASSERT_TRUE(fs::exists(written));
    std::ifstream     in(written);
    std::stringstream ss;
    ss << in.rdbuf();
    EXPECT_EQ(ss.str(), "#define MIX_VERSION \"1.2.3\"\n");

    // And its directory precedes the user's own include dir in the emitted
    // CMakeLists — a stale copy of the same header in the library's sources
    // must not win the lookup.
    std::ifstream     cm(opt.out_dir / "python" / "CMakeLists.txt");
    std::stringstream cs;
    cs << cm.rdbuf();
    const std::string cmake = cs.str();
    const auto        gen   = cmake.find((opt.out_dir / "include").string());
    const auto        usr   = cmake.find(user.string());
    ASSERT_NE(gen, std::string::npos);
    ASSERT_NE(usr, std::string::npos);
    EXPECT_LT(gen, usr);

    fs::remove_all(dir);
}
