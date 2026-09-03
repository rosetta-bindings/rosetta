// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for manifest-local VARIABLES in the manifest loader
// (tools/rosetta_gen/manifest.cpp): a "variables" array declaring names that
// `$NAME` / `${NAME}` then stand for anywhere else in the document, so a long
// shared prefix — a toolchain root spelled into four fields — is written once.
//
// The subject here is the TOOL rather than the runtime, so the suite is plain
// C++ with no reflection and no annotations: it writes a manifest into a temp
// directory and inspects the Manifest that load() returns.
//
// What the substitution must get right, and what each test pins down: that it
// reaches every field (not only the toolchain paths that motivated it), that a
// declaration may build on the ones before it, and — the load-bearing half —
// that it leaves CMake's own two dollar notations alone, since `$ENV{HOME}`
// and `${CMAKE_SOURCE_DIR}` are expanded at CMake's configure time and must
// reach it exactly as written.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <manifest.h>
#include <stdexcept>
#include <string>

namespace {

    // One throwaway manifest per test, removed when the test ends. `body` is
    // the manifest's own members (each with its trailing comma); the required
    // ones and a minimal `classes` are supplied here.
    struct Tree {
        fs::path root;

        explicit Tree(const std::string &tag) {
            static int n = 0;
            root         = fs::temp_directory_path() /
                   ("rosetta_manifest_vars_" + tag + "_" + std::to_string(++n));
            fs::remove_all(root);
            fs::create_directories(root);
            std::ofstream(root / "Point.h") << "struct Point {};\n";
        }
        ~Tree() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        fs::path manifest(const std::string &body) const {
            const fs::path p = root / "manifest.json";
            std::ofstream(p) << "{\n"
                             << "  \"user_include\": \".\",\n"
                             << "  \"rosetta_include\": \".\",\n"
                             << "  \"targets\": [\"python\"],\n"
                             << body << "  \"classes\": [{\"header\": \"Point.h\"}]\n}\n";
            return p;
        }
    };

    // The four toolchain fields the feature was asked for, declared through one
    // variable instead of four copies of the same prefix.
    const char *kToolchain = R"json(
  "variables": [
    {"name": "P2996", "value": "$ENV{HOME}/devs/c++/clang-p2996"}
  ],
  "cpp26_root": "$P2996/build",
  "cpp26_cxx": "$P2996/build/bin/clang++",
  "cpp26_cc": "${P2996}/build/bin/clang",
  "cpp26_lib": "$P2996/build/lib",
)json";

} // namespace

// The motivating case: one declaration, four fields, both spellings of a
// reference. Note what is NOT expanded — `$ENV{HOME}` came through the
// variable's own value untouched, because it is CMake's to expand later.
TEST(ManifestVars, OneDeclarationFillsTheToolchainFields) {
    Tree           t("toolchain");
    const Manifest m = load(t.manifest(kToolchain));

    EXPECT_EQ(m.cpp26_root, "$ENV{HOME}/devs/c++/clang-p2996/build");
    EXPECT_EQ(m.cpp26_cxx, "$ENV{HOME}/devs/c++/clang-p2996/build/bin/clang++");
    EXPECT_EQ(m.cpp26_cc, "$ENV{HOME}/devs/c++/clang-p2996/build/bin/clang");
    EXPECT_EQ(m.cpp26_lib, "$ENV{HOME}/devs/c++/clang-p2996/build/lib");
}

// The substitution is over the whole document, not a list of path-shaped keys:
// a variable works in any string field, here a name and a preprocessor define.
TEST(ManifestVars, SubstitutionReachesEveryField) {
    Tree t("every_field");
    const Manifest m = load(t.manifest(R"json(
  "variables": [{"name": "LIB", "value": "geom"}],
  "generator_name": "$LIB_gen",
  "module_name": "${LIB}_py",
  "compile_definitions": ["LIB_NAME=$LIB"],
)json"));

    // "$LIB_gen" is ONE identifier — `LIB_gen` — and nothing declares it, so it
    // stays verbatim. This is why ${...} is the form to reach for.
    EXPECT_EQ(m.generator_name, "$LIB_gen");
    EXPECT_EQ(m.targets.size(), 1u);
    EXPECT_EQ(m.targets[0].name, "geom_py");
    EXPECT_EQ(m.compile_definitions, (std::vector<std::string>{"LIB_NAME=geom"}));
}

// Declarations are ordered, which is what the array form buys: a later value
// may be written in terms of an earlier one.
TEST(ManifestVars, ADeclarationMayUseTheOnesBeforeIt) {
    Tree t("chained");
    const Manifest m = load(t.manifest(R"json(
  "variables": [
    {"name": "ROOT", "value": "/opt/tc"},
    {"name": "BIN", "value": "$ROOT/build/bin"}
  ],
  "cpp26_root": "$ROOT/build",
  "cpp26_cxx": "$BIN/clang++",
)json"));

    EXPECT_EQ(m.cpp26_root, "/opt/tc/build");
    EXPECT_EQ(m.cpp26_cxx, "/opt/tc/build/bin/clang++");
}

// ...and only the ones before it: a forward reference is not a declaration, so
// it is left alone rather than silently resolving.
TEST(ManifestVars, AForwardReferenceIsNotResolved) {
    Tree t("forward");
    const Manifest m = load(t.manifest(R"json(
  "variables": [
    {"name": "BIN", "value": "$ROOT/bin"},
    {"name": "ROOT", "value": "/opt/tc"}
  ],
  "cpp26_cxx": "$BIN/clang++",
)json"));

    EXPECT_EQ(m.cpp26_cxx, "$ROOT/bin/clang++");
}

// The half that keeps this compatible: CMake's own notations pass through. An
// undeclared ${...} is a CMake variable reference the generated CMakeLists
// expands at configure time, and rewriting or rejecting it would break every
// manifest that has one.
TEST(ManifestVars, CMakeNotationsPassThroughUntouched) {
    Tree t("cmake");
    const Manifest m = load(t.manifest(R"json(
  "variables": [{"name": "P", "value": "/opt/tc"}],
  "cpp26_root": "${CMAKE_SOURCE_DIR}/$P",
  "cpp26_cxx": "$ENV{P}/clang++",
  "cpp26_cc": "$ENV{HOME}/$P/clang",
)json"));

    EXPECT_EQ(m.cpp26_root, "${CMAKE_SOURCE_DIR}//opt/tc");
    EXPECT_EQ(m.cpp26_cxx, "$ENV{P}/clang++"); // ENV{P} is the ENVIRONMENT's P
    EXPECT_EQ(m.cpp26_cc, "$ENV{HOME}//opt/tc/clang");
}

// A manifest with no "variables" is byte-for-byte what it always was — the
// dollar signs in it are nobody's but CMake's.
TEST(ManifestVars, WithoutTheFieldNothingIsSubstituted) {
    Tree t("absent");
    const Manifest m = load(t.manifest(R"json(
  "cpp26_root": "$ENV{HOME}/devs/c++/clang-p2996/build",
  "cpp26_cxx": "${SOME_CMAKE_VAR}/clang++",
)json"));

    EXPECT_EQ(m.cpp26_root, "$ENV{HOME}/devs/c++/clang-p2996/build");
    EXPECT_EQ(m.cpp26_cxx, "${SOME_CMAKE_VAR}/clang++");
}

// Variables reach the paths the LOADER itself resolves, not only the strings it
// carries verbatim to a backend: user_include is made absolute during load().
TEST(ManifestVars, SubstitutionHappensBeforePathsAreResolved) {
    Tree t("resolved");
    fs::create_directories(t.root / "sub");
    std::ofstream(t.root / "sub" / "Point.h") << "struct Point {};\n";

    const Manifest m = load(t.manifest(R"json(
  "variables": [{"name": "HERE", "value": "sub"}],
  "user_include": "$HERE",
)json"));

    ASSERT_EQ(m.user_include.size(), 1u);
    EXPECT_EQ(m.user_include[0], fs::weakly_canonical(t.root / "sub"));
}

// The malformed forms. Each is a mistake the author can only have made once,
// so each says what the shape is rather than failing later on a path that does
// not exist.
TEST(ManifestVars, MalformedDeclarationsAreRejected) {
    {
        Tree t("not_array");
        EXPECT_THROW(load(t.manifest("  \"variables\": {\"P\": \"/opt\"},\n")),
                     std::runtime_error);
    }
    {
        Tree t("no_value");
        EXPECT_THROW(load(t.manifest("  \"variables\": [{\"name\": \"P\"}],\n")),
                     std::runtime_error);
    }
    {
        Tree t("bad_name");
        EXPECT_THROW(load(t.manifest("  \"variables\": [{\"name\": \"2P\", \"value\": \"x\"}],\n")),
                     std::runtime_error);
    }
    {
        Tree t("duplicate");
        EXPECT_THROW(load(t.manifest("  \"variables\": [{\"name\": \"P\", \"value\": \"a\"},"
                                     "{\"name\": \"P\", \"value\": \"b\"}],\n")),
                     std::runtime_error);
    }
    {
        // $ENV{...} is CMake's environment lookup. Letting a manifest declare
        // ENV would make it ambiguous, and the passthrough rule means the
        // declaration could never win anyway — so it is an error, not a no-op.
        Tree t("env");
        EXPECT_THROW(load(t.manifest("  \"variables\": [{\"name\": \"ENV\", \"value\": \"x\"}],\n")),
                     std::runtime_error);
    }
}
