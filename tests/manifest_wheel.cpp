// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for PER-TARGET packaging in the manifest loader
// (tools/rosetta_gen/manifest.cpp): "wheel" and "wheel_dir" on a python /
// nanobind entry of "targets", where they used to be top-level keys, and the
// wheel's CONTENTS — "wheel_files" / "wheel_dependencies" / "wheel_scripts"
// (second half of the file).
//
// The subject here is the TOOL rather than the runtime, so the suite is plain
// C++ with no reflection and no annotations: it writes a manifest into a temp
// directory and inspects the Manifest that load() returns.
//
// What the move must get right, and what each test pins down: that the two
// python backends can now differ (the case the change exists for), that a
// directory is resolved like every other manifest path, that a backend with no
// make_wheel.py to run is told so rather than silently ignoring the key, and —
// the one that matters most — that a manifest carrying the OLD top-level
// spelling fails loudly. A project that used to ship wheels must not quietly
// stop shipping them.

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
                   ("rosetta_manifest_wheel_" + tag + "_" + std::to_string(++n));
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
                             << "  \"module_name\": \"geom\",\n"
                             << body << "  \"classes\": [{\"header\": \"Point.h\"}]\n}\n";
            return p;
        }
    };

    const TargetEntry &target_of(const Manifest &m, const std::string &lang) {
        for (const auto &t : m.targets) {
            if (t.lang == lang) {
                return t;
            }
        }
        throw std::runtime_error("no such target: " + lang);
    }

} // namespace

// The case the move exists for: nanobind emits one abi3 wheel covering every
// CPython 3.12+ and pybind11 one wheel per version, so a manifest may well want
// to ship one and not the other. A top-level flag could not say that.
TEST(ManifestWheel, TheTwoPythonTargetsCanDiffer) {
    Tree           t("differ");
    const Manifest m = load(t.manifest(R"json(
  "targets": [
    { "lang": "python",   "name": "pygeom" },
    { "lang": "nanobind", "name": "nbgeom", "wheel": true }
  ],
)json"));

    EXPECT_FALSE(target_of(m, "python").wheel);
    EXPECT_TRUE(target_of(m, "nanobind").wheel);
}

// A wheel_dir is a path like any other in the manifest: relative to the
// manifest's own directory, resolved to absolute at load time.
TEST(ManifestWheel, WheelDirResolvesAgainstTheManifest) {
    Tree           t("dir");
    const Manifest m = load(t.manifest(R"json(
  "targets": [
    { "lang": "nanobind", "wheel_dir": "./dist/wheels" }
  ],
)json"));

    EXPECT_EQ(target_of(m, "nanobind").wheel_dir,
              fs::weakly_canonical(t.root / "dist" / "wheels").string());
}

// Two targets, two destinations — also unreachable through one global key.
TEST(ManifestWheel, EachTargetKeepsItsOwnDirectory) {
    Tree           t("two_dirs");
    const Manifest m = load(t.manifest(R"json(
  "targets": [
    { "lang": "python",   "wheel_dir": "./dist/pybind" },
    { "lang": "nanobind", "wheel_dir": "./dist/nano" }
  ],
)json"));

    EXPECT_EQ(target_of(m, "python").wheel_dir,
              fs::weakly_canonical(t.root / "dist" / "pybind").string());
    EXPECT_EQ(target_of(m, "nanobind").wheel_dir,
              fs::weakly_canonical(t.root / "dist" / "nano").string());
}

// Saying nothing packages nothing: --wheel on the command line is still the
// way to get a one-off wheel out of a manifest that never asked for one.
TEST(ManifestWheel, AbsentMeansNoPackaging) {
    Tree           t("absent");
    const Manifest m = load(t.manifest("  \"targets\": [\"python\", \"nanobind\"],\n"));

    for (const char *lang : {"python", "nanobind"}) {
        EXPECT_FALSE(target_of(m, lang).wheel) << lang;
        EXPECT_TRUE(target_of(m, lang).wheel_dir.empty()) << lang;
    }
}

// The migration guard. These were top-level keys, and a key the loader merely
// ignored would mean a manifest that used to ship wheels silently stopping —
// the one failure mode worth an error rather than a warning.
TEST(ManifestWheel, TopLevelSpellingIsRejected) {
    {
        Tree t("top_wheel");
        EXPECT_THROW(load(t.manifest("  \"wheel\": true,\n"
                                     "  \"targets\": [\"python\"],\n")),
                     std::runtime_error);
    }
    {
        Tree t("top_dir");
        EXPECT_THROW(load(t.manifest("  \"wheel_dir\": \"./dist\",\n"
                                     "  \"targets\": [\"python\"],\n")),
                     std::runtime_error);
    }
}

// Only the two backends that emit a make_wheel.py can honour these, so a
// markdown target asking for a wheel is a mistake worth naming.
TEST(ManifestWheel, RejectedOnABackendThatCannotPackage) {
    {
        Tree t("markdown");
        EXPECT_THROW(load(t.manifest(
                         "  \"targets\": [{\"lang\": \"markdown\", \"wheel\": true}],\n")),
                     std::runtime_error);
    }
    {
        Tree t("node");
        EXPECT_THROW(load(t.manifest(
                         "  \"targets\": [{\"lang\": \"node\", \"wheel_dir\": \"./d\"}],\n")),
                     std::runtime_error);
    }
}

// A deprecated spelling is folded to its canonical lang before the check, so
// "python-expanded" is still a packaging backend.
TEST(ManifestWheel, DeprecatedAliasStillPackages) {
    Tree           t("alias");
    const Manifest m = load(t.manifest(
        "  \"targets\": [{\"lang\": \"python-expanded\", \"wheel\": true}],\n"));

    ASSERT_EQ(m.targets.size(), 1u);
    EXPECT_EQ(m.targets[0].lang, "python");
    EXPECT_TRUE(m.targets[0].wheel);
}

// An empty directory is a typo, not a request for the default.
TEST(ManifestWheel, EmptyWheelDirIsRejected) {
    Tree t("empty");
    EXPECT_THROW(load(t.manifest(
                     "  \"targets\": [{\"lang\": \"nanobind\", \"wheel_dir\": \"\"}],\n")),
                 std::runtime_error);
}

// ---------------------------------------------------------------------------
// Wheel CONTENTS: "wheel_files" / "wheel_dependencies" / "wheel_scripts". The
// case they exist for is a pure-Python layer that belongs with the binding —
// helpers, a bridge, a data file — shipping in the SAME wheel as the module
// instead of as a second package the user has to know about.
// ---------------------------------------------------------------------------

// A bare string is a source with the default destination (the wheel root);
// the object form names a subdirectory. Both resolve against the manifest.
TEST(ManifestWheel, WheelFilesResolveAndDefaultToTheRoot) {
    Tree t("files");
    fs::create_directories(t.root / "python" / "geom_py");
    std::ofstream(t.root / "python" / "geom_py" / "__init__.py") << "\n";
    std::ofstream(t.root / "native.js") << "// js\n";

    const Manifest m = load(t.manifest(R"json(
  "targets": [
    { "lang": "nanobind", "wheel_files": [
        "./python/geom_py",
        {"path": "./native.js", "dest": "geom_py/web/"} ] }
  ],
)json"));

    const auto &files = target_of(m, "nanobind").wheel_files;
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].path, fs::weakly_canonical(t.root / "python" / "geom_py").string());
    EXPECT_EQ(files[0].dest, ".");
    EXPECT_EQ(files[1].path, fs::weakly_canonical(t.root / "native.js").string());
    EXPECT_EQ(files[1].dest, "geom_py/web") << "normalised, no trailing slash";
}

// The manifest promised to ship it: a source that is not there is an error at
// load time, not a wheel that is quietly missing a package.
TEST(ManifestWheel, WheelFilesMustExist) {
    Tree t("files_missing");
    EXPECT_THROW(load(t.manifest(
                     "  \"targets\": [{\"lang\": \"python\", "
                     "\"wheel_files\": [\"./python/nope\"]}],\n")),
                 std::runtime_error);
}

// The destination is inside the wheel by construction: an absolute path or a
// ".." would escape the install prefix scikit-build-core packages.
TEST(ManifestWheel, WheelFilesDestStaysInsideTheWheel) {
    {
        Tree t("dest_abs");
        std::ofstream(t.root / "a.txt") << "\n";
        EXPECT_THROW(load(t.manifest(
                         "  \"targets\": [{\"lang\": \"python\", \"wheel_files\": "
                         "[{\"path\": \"./a.txt\", \"dest\": \"/usr/lib\"}]}],\n")),
                     std::runtime_error);
    }
    {
        Tree t("dest_up");
        std::ofstream(t.root / "a.txt") << "\n";
        EXPECT_THROW(load(t.manifest(
                         "  \"targets\": [{\"lang\": \"python\", \"wheel_files\": "
                         "[{\"path\": \"./a.txt\", \"dest\": \"pkg/../../x\"}]}],\n")),
                     std::runtime_error);
    }
}

// Dependencies are requirement strings kept verbatim; scripts map a command to
// "module:function" and nothing else is accepted.
TEST(ManifestWheel, DependenciesAndScripts) {
    Tree           t("deps");
    const Manifest m = load(t.manifest(R"json(
  "targets": [
    { "lang": "python",
      "wheel_dependencies": ["numpy>=1.20", "websockets"],
      "wheel_scripts": {"geom-serve": "geom_py.serve:main"} }
  ],
)json"));

    const auto &py = target_of(m, "python");
    EXPECT_EQ(py.wheel_dependencies, (std::vector<std::string>{"numpy>=1.20", "websockets"}));
    ASSERT_EQ(py.wheel_scripts.size(), 1u);
    EXPECT_EQ(py.wheel_scripts.at("geom-serve"), "geom_py.serve:main");

    Tree bad("deps_bad");
    EXPECT_THROW(load(bad.manifest(
                     "  \"targets\": [{\"lang\": \"python\", "
                     "\"wheel_scripts\": {\"geom-serve\": \"no_colon_here\"}}],\n")),
                 std::runtime_error);
}

// Same rules as wheel / wheel_dir: per-target only, packaging backends only.
TEST(ManifestWheel, ContentsFollowTheSameGuards) {
    {
        Tree t("contents_top");
        EXPECT_THROW(load(t.manifest("  \"wheel_dependencies\": [\"numpy\"],\n"
                                     "  \"targets\": [\"python\"],\n")),
                     std::runtime_error);
    }
    {
        Tree t("contents_node");
        EXPECT_THROW(load(t.manifest("  \"targets\": [{\"lang\": \"node\", "
                                     "\"wheel_dependencies\": [\"numpy\"]}],\n")),
                     std::runtime_error);
    }
    {
        Tree t("contents_absent");
        const Manifest m = load(t.manifest("  \"targets\": [\"nanobind\"],\n"));
        EXPECT_TRUE(target_of(m, "nanobind").wheel_files.empty());
        EXPECT_TRUE(target_of(m, "nanobind").wheel_dependencies.empty());
        EXPECT_TRUE(target_of(m, "nanobind").wheel_scripts.empty());
    }
}
