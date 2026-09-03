// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for PER-TARGET packaging in the manifest loader
// (tools/rosetta_gen/manifest.cpp): "wheel" and "wheel_dir" on a python /
// nanobind entry of "targets", where they used to be top-level keys.
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
