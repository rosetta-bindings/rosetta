// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

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
        static int     n   = 0;
        const fs::path dir = fs::temp_directory_path() / ("rosetta_pins_" + std::to_string(++n));
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
    for (const char *lang : {"python-expanded", "nanobind-expanded", "python", "nanobind"}) {
        const std::string bare =
            emitted(lang, "CMakeLists.txt", [](rosetta::GenContext &c) { c.python = "3.11"; });
        EXPECT_NE(bare.find("COMMAND python3.11 -c \"import sys; print(sys.executable)\""),
                  std::string::npos)
            << lang << " ignored a bare version pin";

        const std::string path = emitted(lang, "CMakeLists.txt", [](rosetta::GenContext &c) {
            c.python = "/opt/py/bin/python3";
        });
        EXPECT_NE(path.find("COMMAND /opt/py/bin/python3 -c"), std::string::npos)
            << lang << " ignored an interpreter path";

        // Unpinned keeps the old behaviour exactly.
        EXPECT_NE(emitted(lang, "CMakeLists.txt", none).find("COMMAND python3 -c"),
                  std::string::npos)
            << lang << " changed the default probe";
    }
}

// One field feeds BOTH the find_package floor and pyproject's requires-python,
// which is the point: they cannot drift apart.
TEST(RuntimePins, RequiresPythonFeedsTheFloorAndTheWheelMetadata) {
    const auto pin = [](rosetta::GenContext &c) { c.requires_python = ">=3.10"; };

    const std::string cm = emitted("nanobind-expanded", "CMakeLists.txt", pin);
    EXPECT_NE(cm.find("find_package(Python 3.10 COMPONENTS"), std::string::npos) << cm;

    for (const char *lang : {"python-expanded", "nanobind-expanded"}) {
        EXPECT_NE(emitted(lang, "pyproject.toml", pin).find("requires-python = \">=3.10\""),
                  std::string::npos)
            << lang;
        EXPECT_NE(emitted(lang, "pyproject.toml", none).find("requires-python = \">=3.8\""),
                  std::string::npos)
            << lang << " changed the default";
    }
    EXPECT_NE(emitted("nanobind-expanded", "CMakeLists.txt", none).find("find_package(Python 3.8 "),
              std::string::npos);
}

// N-API version reaches the compile definition; the engines entry reaches
// package.json as valid JSON, and is absent (no dangling comma) when unset.
TEST(RuntimePins, NodeVersionKnobs) {
    for (const char *lang : {"node-expanded", "node"}) {
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
