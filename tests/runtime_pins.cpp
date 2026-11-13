// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for the per-target runtime pins (manifest "python",
// "requires_python", "napi_version", "node_engine").
//
// Before these, the generated project probed whatever `python3` and `node` the
// PATH happened to resolve, with a 3.8 floor and N-API 8 written into the
// templates as literals — and the obvious escape hatch did not work, because
// the CMake sets Python_EXECUTABLE with CACHE ... FORCE, which overrides a
// -DPython_EXECUTABLE= passed on the command line. The pin is therefore the
// only way to say which interpreter a binding is built for.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <sstream>
#include <string>

namespace rp {
    class Thing {
    public:
        Thing() = default;
        double value() const { return 0.0; }
    };
} // namespace rp

template <> struct rosetta::binding_info<rp::Thing> {
    static constexpr const char *header = "Thing.h";
};

namespace {

    namespace fs = std::filesystem;

    // Emit a backend's project into a temp tree; hand back one named file.
    std::string emitted(const char *lang, const std::string &file,
                        const std::function<void(rosetta::GenContext &)> &pin) {
        // Named after the running test: ctest -j runs every test in its own
        // process, so a per-process counter alone collides across processes.
        static int     n   = 0;
        const fs::path dir = fs::temp_directory_path() /
                             ("rosetta_pins_" +
                              std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) +
                              "_" + std::to_string(++n));
        fs::remove_all(dir);

        auto c    = rosetta::gen_detail::make_context<rp::Thing>("pintest");
        c.out_dir = dir;
        pin(c);
        rosetta::backend_registry().at(lang)->emit(c);

        std::string out;
        for (const auto &e : fs::recursive_directory_iterator(dir)) {
            if (e.path().filename() == file) {
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

    const auto none = [](rosetta::GenContext &) {};

} // namespace

// A bare version becomes pythonX.Y; a path goes in as written. The probe stays
// either way — asking the interpreter for its own sys.executable is what turns
// a PATH name into the absolute path find_package needs.
TEST(RuntimePins, PythonInterpreterIsPinned) {
    for (const char *lang : {"python", "nanobind"}) {
        const std::string bare =
            emitted(lang, "CMakeLists.txt", [](rosetta::GenContext &c) { c.python = "3.11"; });
        EXPECT_NE(bare.find("set(_rosetta_python_cmd \"python3.11\")"), std::string::npos)
            << lang << " ignored a bare version pin";
        EXPECT_NE(bare.find("COMMAND \"${_rosetta_python_cmd}\" -c \"import sys; "
                            "print(sys.executable)\""),
                  std::string::npos)
            << lang << " lost the probe";

        const std::string path = emitted(lang, "CMakeLists.txt", [](rosetta::GenContext &c) {
            c.python = "/opt/py/bin/python3";
        });
        EXPECT_NE(path.find("set(_rosetta_python_cmd \"/opt/py/bin/python3\")"),
                  std::string::npos)
            << lang << " ignored an interpreter path";

        // Unpinned probes python3, as before.
        EXPECT_NE(emitted(lang, "CMakeLists.txt", none).find("set(_rosetta_python_cmd \"python3\")"),
                  std::string::npos)
            << lang << " changed the default probe";
    }
}

// The pin is written where the bindings are GENERATED; they may be built on
// another machine (generated on macOS, built on Windows). -DROSETTA_PYTHON wins
// over the pin, a pin that does not run there is reported and find_package
// chooses, and a ROSETTA_PYTHON that does not run is an error — never an empty
// interpreter silently forced into the cache.
TEST(RuntimePins, PythonPinCanBeOverriddenAndFallsBack) {
    const auto pin = [](rosetta::GenContext &c) { c.python = "/Users/me/.pyenv/bin/python3"; };
    for (const char *lang : {"python", "nanobind"}) {
        const std::string cm = emitted(lang, "CMakeLists.txt", pin);
        const auto        at = [&](const char *s) { return cm.find(s); };
        EXPECT_NE(at("set(ROSETTA_PYTHON \"\" CACHE FILEPATH"), std::string::npos) << lang;
        EXPECT_LT(at("if(ROSETTA_PYTHON)"), at("set(_rosetta_python_cmd \"/Users/me")) << lang;
        EXPECT_NE(at("if(_rosetta_python_rc EQUAL 0 AND _rosetta_python)"), std::string::npos)
            << lang << " forces the interpreter without checking the probe ran";
        EXPECT_NE(at("message(FATAL_ERROR \"ROSETTA_PYTHON="), std::string::npos) << lang;
        EXPECT_NE(at("message(WARNING \"The pinned Python"), std::string::npos) << lang;
        EXPECT_NE(at("find_package(Python 3 COMPONENTS Interpreter REQUIRED)"), std::string::npos)
            << lang << " has no fallback when the pin does not run";
        // the pybind11 template reads both names
        EXPECT_NE(at("set(Python3_EXECUTABLE \"${_rosetta_python}\" CACHE FILEPATH \"\" FORCE)"),
                  std::string::npos)
            << lang;
    }
}

// The manifest's optimization level must not reach Debug (MSVC: D8016 with
// /RTC1) nor the MSVC link line (LNK4044), and MSVC gets its own /O spelling.
TEST(RuntimePins, OptimizationIsPerCompilerAndNotInDebug) {
    const std::string cm = emitted("nanobind", "CMakeLists.txt",
                                   [](rosetta::GenContext &c) { c.optimization = "-O2"; });
    EXPECT_EQ(cm.find("add_compile_options(-O2)"), std::string::npos) << cm;
    EXPECT_EQ(cm.find("add_link_options(-O2)"), std::string::npos) << cm;
    EXPECT_NE(cm.find("if(MSVC)\n    add_compile_options($<$<NOT:$<CONFIG:Debug>>:/O2>)\n"
                      "else()\n    add_compile_options($<$<NOT:$<CONFIG:Debug>>:-O2>)\n"
                      "    add_link_options($<$<NOT:$<CONFIG:Debug>>:-O2>)\nendif()"),
              std::string::npos)
        << cm;

    const auto msvc_of = [&](const char *level) {
        const std::string out = emitted("nanobind", "CMakeLists.txt",
                                        [&](rosetta::GenContext &c) { c.optimization = level; });
        const std::string head = "if(MSVC)\n    add_compile_options($<$<NOT:$<CONFIG:Debug>>:";
        const auto        b    = out.find(head);
        return b == std::string::npos ? std::string() : out.substr(b + head.size(), 3);
    };
    EXPECT_EQ(msvc_of("-O0"), "/Od");
    EXPECT_EQ(msvc_of("-Os"), "/O1");
    EXPECT_EQ(msvc_of("-O3"), "/O2");

    // no manifest level, no optimization block at all
    EXPECT_EQ(emitted("nanobind", "CMakeLists.txt", none).find("if(MSVC)"), std::string::npos);
}

// One field feeds BOTH the find_package floor and pyproject's requires-python,
// which is the point: they cannot drift apart.
TEST(RuntimePins, RequiresPythonFeedsTheFloorAndTheWheelMetadata) {
    const auto pin = [](rosetta::GenContext &c) { c.requires_python = ">=3.10"; };

    const std::string cm = emitted("nanobind", "CMakeLists.txt", pin);
    EXPECT_NE(cm.find("find_package(Python 3.10 COMPONENTS"), std::string::npos) << cm;

    for (const char *lang : {"python", "nanobind"}) {
        EXPECT_NE(emitted(lang, "pyproject.toml", pin).find("requires-python = \">=3.10\""),
                  std::string::npos)
            << lang;
        EXPECT_NE(emitted(lang, "pyproject.toml", none).find("requires-python = \">=3.8\""),
                  std::string::npos)
            << lang << " changed the default";
    }
    EXPECT_NE(emitted("nanobind", "CMakeLists.txt", none).find("find_package(Python 3.8 "),
              std::string::npos);
}

// N-API version reaches the compile definition; the engines entry reaches
// package.json as valid JSON, and is absent (no dangling comma) when unset.
TEST(RuntimePins, NodeVersionKnobs) {
    for (const char *lang : {"node"}) {
        const std::string cm = emitted(lang, "CMakeLists.txt", [](rosetta::GenContext &c) {
            c.napi_version = "9";
        });
        EXPECT_NE(cm.find("NAPI_VERSION=9"), std::string::npos) << lang;
        EXPECT_NE(emitted(lang, "CMakeLists.txt", none).find("NAPI_VERSION=8"), std::string::npos)
            << lang << " changed the default";

        const std::string pkg = emitted(lang, "package.json", [](rosetta::GenContext &c) {
            c.node_engine = ">=18";
        });
        EXPECT_NE(pkg.find("\"engines\": {"), std::string::npos) << lang;
        EXPECT_NE(pkg.find("\"node\": \">=18\""), std::string::npos) << lang;
        EXPECT_NE(pkg.find("}\n}"), std::string::npos) << "engines block left invalid JSON: " << pkg;

        const std::string plain = emitted(lang, "package.json", none);
        EXPECT_EQ(plain.find("engines"), std::string::npos) << lang;
        EXPECT_EQ(plain.find(",\n}"), std::string::npos) << "dangling comma: " << plain;
    }
}
