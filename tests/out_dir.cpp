// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for the per-target artifact directory (manifest target
// "out_dir" → TargetSpec::artifact_dir → {{OUT_DIR_BLOCK}}).
//
// The generated project already drops its module next to its own sources, which
// is convenient for a smoke test and useless for a project that wants the .so
// inside its Python package (or the .js next to a web app's assets). Naming a
// directory copies the built artifact there after every build, so the binding
// needs no copy step of its own.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

namespace od {
    class Thing {
    public:
        Thing() = default;
        double value() const { return 0.0; }
    };
} // namespace od

template <> struct rosetta::binding_info<od::Thing> {
    static constexpr const char *header = "Thing.h";
};

namespace {

    namespace fs = std::filesystem;

    // Emit one backend's project into a temp tree and hand back its CMakeLists.
    std::string cmake_of(const char *lang, const std::string &artifact_dir) {
        static int          n   = 0;
        const fs::path      dir = fs::temp_directory_path() /
                             ("rosetta_out_dir_" + std::to_string(++n));
        fs::remove_all(dir);

        auto c         = rosetta::gen_detail::make_context<od::Thing>("odtest");
        c.out_dir      = dir;
        c.artifact_dir = artifact_dir;
        rosetta::backend_registry().at(lang)->emit(c);

        std::string  out;
        for (const auto &e : fs::recursive_directory_iterator(dir)) {
            if (e.path().filename() == "CMakeLists.txt") {
                std::ifstream     in(e.path());
                std::stringstream ss;
                ss << in.rdbuf();
                out = ss.str();
                break;
            }
        }
        fs::remove_all(dir);
        return out;
    }

} // namespace

// Every backend that builds a loadable artifact honours the directory.
TEST(OutDir, ArtifactIsCopiedToTheNamedDirectory) {
    for (const char *lang : {"python", "nanobind", "node", "lua"}) {
        const std::string cm = cmake_of(lang, "/tmp/rosetta-artifacts");
        ASSERT_FALSE(cm.empty()) << lang << " emitted no CMakeLists.txt";
        EXPECT_NE(cm.find("# Artifact output directory (manifest \"out_dir\")."),
                  std::string::npos)
            << lang << " ignored the artifact directory";
        EXPECT_NE(cm.find("-E make_directory \"/tmp/rosetta-artifacts\""), std::string::npos)
            << lang << " does not create the directory";
        EXPECT_NE(cm.find("$<TARGET_FILE:odtest> \"/tmp/rosetta-artifacts\""), std::string::npos)
            << lang << " does not copy the built artifact there";
    }
}

// wasm ships a PAIR — the .js loader is the CMake target file, the .wasm is its
// sibling and has to be named on its own or the copy is useless.
TEST(OutDir, WasmCopiesBothHalvesOfTheModule) {
    const std::string cm = cmake_of("wasm", "/tmp/rosetta-artifacts");
    ASSERT_FALSE(cm.empty());
    EXPECT_NE(cm.find("$<TARGET_FILE:odtest> \"/tmp/rosetta-artifacts\""), std::string::npos);
    EXPECT_NE(cm.find("$<TARGET_FILE_DIR:odtest>/odtest.wasm \"/tmp/rosetta-artifacts\""),
              std::string::npos)
        << "the .wasm was left behind: " << cm;
}

// Unset (the default) emits nothing at all — no stray copy step, no empty
// make_directory, and the existing next-to-the-sources copy is untouched.
TEST(OutDir, UnsetEmitsNothing) {
    for (const char *lang : {"python", "nanobind", "node",
                             "wasm", "lua"}) {
        const std::string cm = cmake_of(lang, "");
        ASSERT_FALSE(cm.empty()) << lang;
        EXPECT_EQ(cm.find("Artifact output directory"), std::string::npos) << lang;
        EXPECT_EQ(cm.find("make_directory"), std::string::npos) << lang;
    }
}
