// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for class-entry GLOBS in the manifest loader
// (tools/rosetta_gen/manifest.cpp): an entry whose "header" carries glob magic
// — {"header": "geom/*.h"} — standing for every header it matches, one class
// entry per file named from the file's stem.
//
// The subject here is the TOOL rather than the runtime, so the suite is plain
// C++ with no reflection and no annotations: it writes a header tree and a
// manifest into a temp directory and inspects the Manifest that load() returns.
//
// What the glob must get right, and what each test pins down: which files it
// takes (headers only, recursion only where asked, "exclude" honoured), what
// it names them (the stem, qualified by the namespace in scope, skipped when
// the stem is no identifier), how it composes with entries spelled out (they
// win, by header AND by name), and what it refuses (the per-class fields,
// which cannot mean anything for a folder).

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <manifest.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

    // One throwaway project per test: files written under a temp root, a
    // manifest beside them, both removed when the test ends.
    struct Tree {
        fs::path root;

        explicit Tree(const std::string &tag) {
            static int n = 0;
            root         = fs::temp_directory_path() /
                   ("rosetta_manifest_glob_" + tag + "_" + std::to_string(++n));
            fs::remove_all(root);
            fs::create_directories(root);
        }
        ~Tree() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        // A file at `rel` (relative to the root), with contents nothing here
        // reads — load() never opens a bound header, it only lists them.
        const Tree &file(const std::string &rel, const std::string &text = "struct T {};\n") const {
            const fs::path p = root / rel;
            fs::create_directories(p.parent_path());
            std::ofstream(p) << text;
            return *this;
        }

        // The manifest: `classes` is the array verbatim, `extra` any JSON
        // members to put before it (each with its trailing comma), `inc` the
        // "user_include" value — a string or an array, so a test can give the
        // loader two include roots to search.
        fs::path manifest(const std::string &classes, const std::string &extra = "",
                          const std::string &inc = "\"lib\"") const {
            const fs::path p = root / "manifest.json";
            std::ofstream(p) << "{\n"
                             << "  \"user_include\": " << inc << ",\n"
                             << "  \"rosetta_include\": \".\",\n"
                             << "  \"targets\": [\"python\"],\n"
                             << extra << "  \"classes\": " << classes << "\n}\n";
            return p;
        }
    };

    // The two things every assertion below is about: the headers a manifest
    // binds (as emitted into #include "...") and the C++ names it binds them
    // under, both in manifest order.
    std::vector<std::string> headers(const Manifest &m) {
        std::vector<std::string> out;
        for (const auto &c : m.classes) {
            out.push_back(c.header);
        }
        return out;
    }
    std::vector<std::string> names(const Manifest &m) {
        std::vector<std::string> out;
        for (const auto &c : m.classes) {
            out.push_back(c.name);
        }
        return out;
    }

    using Strings = std::vector<std::string>;

} // namespace

// The base case: one entry standing for a folder. Names come from the stems,
// non-headers are not files a class can be derived from, and a single `*` does
// not descend — it is one path component, as in any shell.
TEST(ManifestGlob, OneEntryBindsEveryHeaderInTheFolder) {
    Tree t("folder");
    t.file("lib/Point.h").file("lib/Vector.h").file("lib/notes.txt").file("lib/sub/Deep.h");

    const Manifest m = load(t.manifest(R"([{"header": "*.h"}])"));
    EXPECT_EQ(headers(m), (Strings{"Point.h", "Vector.h"}));
    EXPECT_EQ(names(m), (Strings{"Point", "Vector"}));
}

// `**` descends, and the extension filter is what keeps that from being
// dangerous: "**/*" matches a .cpp and a README too, and neither is a header
// the binding could include.
TEST(ManifestGlob, DoubleStarRecursesAndTakesHeadersOnly) {
    Tree t("recurse");
    t.file("lib/Point.h")
        .file("lib/sub/Deep.h")
        .file("lib/sub/Deeper.hpp")
        .file("lib/Point.cpp")
        .file("lib/README");

    const Manifest m = load(t.manifest(R"([{"header": "**/*"}])"));
    EXPECT_EQ(headers(m), (Strings{"Point.h", "sub/Deep.h", "sub/Deeper.hpp"}));
    EXPECT_EQ(names(m), (Strings{"Point", "Deep", "Deeper"}));
}

// "exclude" — the headers in a folder that declare no bound class, or a whole
// detail subtree. A pattern or a list of them, same syntax and same roots.
TEST(ManifestGlob, ExcludeDropsWhatItMatches) {
    Tree t("exclude");
    t.file("lib/Point.h").file("lib/fwd.h").file("lib/Point_impl.h").file("lib/detail/Guts.h");

    const Manifest one =
        load(t.manifest(R"([{"header": "**/*.h", "exclude": "detail/*.h"}])"));
    EXPECT_EQ(headers(one), (Strings{"Point.h", "Point_impl.h", "fwd.h"}));

    const Manifest many = load(
        t.manifest(R"([{"header": "**/*.h", "exclude": ["detail/*.h", "fwd.h", "*_impl.h"]}])"));
    EXPECT_EQ(headers(many), (Strings{"Point.h"}));
}

// The composability rule, which is what makes a folder glob usable at all: an
// entry spelled out wins, whether the glob would have collided with its HEADER
// (the class needing a side-car) or only with its NAME (a header declaring
// several explicitly-named classes, whose stem names nothing at all).
TEST(ManifestGlob, EntriesSpelledOutWinByHeaderAndByName) {
    Tree t("explicit");
    t.file("lib/Point.h").file("lib/Mesh.h").file("lib/types.h").file("lib/Vector.h");

    const Manifest m = load(t.manifest(R"([
        {"header": "*.h"},
        {"header": "Mesh.h", "expose": "Surface"},
        {"header": "types.h", "name": "Tolerance"}
    ])"));

    // The spelled-out entries keep their place and their fields; the globbed
    // ones follow, minus the two headers already claimed.
    EXPECT_EQ(headers(m), (Strings{"Mesh.h", "types.h", "Point.h", "Vector.h"}));
    EXPECT_EQ(names(m), (Strings{"Mesh", "Tolerance", "Point", "Vector"}));
    EXPECT_EQ(m.classes[0].expose, "Surface");

    // Nothing named "types" was invented — that is the whole point of yielding
    // by header, since types.h declares Tolerance and no class called types.
    const Strings bound = names(m);
    EXPECT_EQ(std::count(bound.begin(), bound.end(), std::string("types")), 0);
}

// The cost of yielding by header, pinned down because it is the one case where
// a glob binds LESS than it looks like it does: an entry naming a second class
// of a matched header takes the file over, so the class named after the file is
// not bound until it is listed too. Loud (a note on stderr), not silent.
TEST(ManifestGlob, AnExplicitEntryTakesOverItsWholeHeader) {
    Tree t("takeover");
    t.file("lib/Point.h").file("lib/Vector.h"); // Point.h declares Point AND Aabb

    const Manifest lost = load(t.manifest(R"([
        {"header": "*.h"},
        {"header": "Point.h", "name": "Aabb"}
    ])"));
    EXPECT_EQ(names(lost), (Strings{"Aabb", "Vector"})); // no "Point"

    // The one-line fix the note asks for.
    const Manifest both = load(t.manifest(R"([
        {"header": "*.h"},
        {"header": "Point.h", "name": "Aabb"},
        {"header": "Point.h", "name": "Point"}
    ])"));
    EXPECT_EQ(names(both), (Strings{"Aabb", "Point", "Vector"}));
    EXPECT_EQ(headers(both), (Strings{"Point.h", "Point.h", "Vector.h"}));
}

// Every include root is searched, and a header reached twice — by two
// overlapping patterns, or under two roots (lib/Point.h and other/Point.h are
// one include path away from being the same #include) — is bound once. A
// repeated class would collide in the generated module.
//
// The order is the deterministic one it looks like: pattern by pattern, root by
// root, sorted within each — so "*.h" contributes lib/ then other/ before
// "**/*.h" reaches the subdirectory.
TEST(ManifestGlob, OverlappingPatternsAndIncludeRootsBindEachHeaderOnce) {
    Tree t("dedup");
    t.file("lib/Point.h").file("lib/sub/Deep.h").file("other/Extra.h").file("other/Point.h");

    const Manifest m = load(t.manifest(R"([{"header": "*.h"}, {"header": "**/*.h"}])", "",
                                       R"(["lib", "other"])"));
    EXPECT_EQ(headers(m), (Strings{"Point.h", "Extra.h", "sub/Deep.h"}));
}

// The derived name is the file's stem, so a stem that is not a C++ identifier
// has nothing to derive from. Skipped with a warning rather than emitted into a
// driver that cannot compile — and the rest of the folder still binds.
TEST(ManifestGlob, HeaderWhoseStemIsNotAnIdentifierIsSkipped) {
    Tree t("stem");
    t.file("lib/Point.h").file("lib/my-utils.h").file("lib/2d.h");

    const Manifest m = load(t.manifest(R"([{"header": "*.h"}])"));
    EXPECT_EQ(headers(m), (Strings{"Point.h"}));
}

// A glob is an ordinary entry, so it sits inside a group and takes that group's
// composed header_dir and namespace exactly as a spelled-out one does.
TEST(ManifestGlob, GlobInheritsTheGroupsHeaderDirAndNamespace) {
    Tree t("group");
    t.file("lib/geom/solvers/Gmres.h").file("lib/geom/Point.h");

    const Manifest m = load(t.manifest(
        R"([{"header_dir": "solvers", "namespace": "solve", "entries": [{"header": "*.h"}]}])",
        "  \"namespace\": \"geom\",\n  \"header_dir\": \"geom\",\n"));
    EXPECT_EQ(headers(m), (Strings{"geom/solvers/Gmres.h"}));
    EXPECT_EQ(names(m), (Strings{"geom::solve::Gmres"}));
}

// "final" is a property of the binding, uniform across a folder, so it carries.
TEST(ManifestGlob, FinalCarriesToEveryMatchedClass) {
    Tree t("final");
    t.file("lib/A.h").file("lib/B.h");

    const Manifest m = load(t.manifest(R"([{"header": "*.h", "final": true}])"));
    ASSERT_EQ(m.classes.size(), 2u);
    EXPECT_TRUE(m.classes[0].final_);
    EXPECT_TRUE(m.classes[1].final_);
}

// The per-class fields cannot mean anything for a whole folder — applying one
// N times would rename N classes to the same thing, or hand them all one
// class's annotation side-car. Refused where the message can name the pattern.
TEST(ManifestGlob, PerClassFieldsAreRejectedOnAGlobEntry) {
    Tree t("reject");
    t.file("lib/Point.h");

    for (const char *entry : {R"({"header": "*.h", "name": "Point"})",
                              R"({"header": "*.h", "expose": "Pt"})",
                              R"({"header": "*.h", "annotations": "Point.ann.json"})",
                              R"({"header": "*.h", "extensions": [{"name": "len",
                                                                   "header": "ext.h"}]})"}) {
        EXPECT_THROW(load(t.manifest(std::string("[") + entry + "]")), std::runtime_error)
            << entry << " was accepted on a glob entry";
    }
}

// A pattern that matches nothing warns rather than throws — the manifest may
// still bind plenty — but it contributes nothing, and a manifest left with no
// class at all fails the existing check.
TEST(ManifestGlob, APatternMatchingNothingIsNotFatalByItself) {
    Tree t("empty");
    t.file("lib/Point.h");

    const Manifest m =
        load(t.manifest(R"([{"header": "Point.h"}, {"header": "nowhere/*.h"}])"));
    EXPECT_EQ(headers(m), (Strings{"Point.h"}));

    EXPECT_THROW(load(t.manifest(R"([{"header": "nowhere/*.h"}])")), std::runtime_error);
}

// The regression guard: a header path with no glob character is taken
// literally, exactly as before — including one naming a file that does not
// exist on disk (a manifest may legitimately name a header found on an include
// path the loader knows nothing about, and load() has never gone looking).
TEST(ManifestGlob, PlainHeaderPathsAreUntouched) {
    Tree t("plain");
    t.file("lib/Point.h");

    const Manifest m = load(t.manifest(R"([
        {"header": "Point.h"},
        {"name": "geom::Mesh", "header": "elsewhere/Mesh.h"}
    ])"));
    EXPECT_EQ(headers(m), (Strings{"Point.h", "elsewhere/Mesh.h"}));
    EXPECT_EQ(names(m), (Strings{"Point", "geom::Mesh"}));
}
