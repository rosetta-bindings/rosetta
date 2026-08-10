// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for the machine-readable COVERAGE REPORT
// (<rosetta/coverage.h>, emitted by generate() as <out_dir>/coverage.json).
//
// rosetta skips what a backend cannot marshal rather than binding something that
// throws at call time. That is the right policy and a silent one: the report is
// what makes each of those decisions visible, so a method that quietly stops
// binding shows up as a diff instead of as a missing call at runtime.
//
// What is pinned here: that a skip is recorded WITH a reason, that the bound
// side is recorded too (so "nothing bound" is distinguishable from "this class
// never reached the backend"), that dropped overloads are attributed, and that
// the JSON is well-formed and parseable.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <rosetta/generate.h>
#include <string>

using json = nlohmann::json;

// Deleting the copy constructor also suppresses the implicit move one, so a
// by-value parameter of this type is something pybind11 cannot marshal.
struct CovNoCopy {
    CovNoCopy()                  = default;
    CovNoCopy(const CovNoCopy &) = delete;
    int x                        = 0;
};

// A class mixing what binds, what a name-keyed backend must drop, and what no
// backend can marshal.
struct CovThing {
    int    value = 0;
    int    get() const { return value; }
    int    get(int scale) const { return value * scale; }   // dropped by name-keyed targets
    void   listen(std::function<void(int)> cb) { cb(value); } // no N-API/embind conversion
    void   take(CovNoCopy c) { value = c.x; }                 // pybind cannot copy the argument
    bool   operator==(const CovThing &o) const { return value == o.value; } // no bindable name
};

template <> struct rosetta::binding_info<CovThing> {
    static constexpr const char *header = "cov.h";
};

namespace {

    // Render one backend with a clean log, then hand back the parsed report.
    json report_for(const char *lang) {
        rosetta::coverage::reset();
        const auto c = rosetta::gen_detail::make_context<CovThing>("covtest");
        rosetta::backend_registry().at(lang)->render(c);
        return json::parse(rosetta::coverage::to_json(c.classes));
    }

    // The per-class node for `scope` under `target`, or a null json.
    json class_node(const json &rep, const char *target, const char *scope) {
        for (const auto &t : rep.at("targets")) {
            if (t.at("target") != target) {
                continue;
            }
            for (const auto &k : t.at("classes")) {
                if (k.at("class") == scope) {
                    return k;
                }
            }
        }
        return json{};
    }

    bool lists_member(const json &arr, const char *member) {
        for (const auto &e : arr) {
            if (e.at("member") == member) {
                return true;
            }
        }
        return false;
    }

    json find_skip(const json &node, const char *member) {
        for (const auto &e : node.at("skipped")) {
            if (e.at("member") == member) {
                return e;
            }
        }
        return json{};
    }

} // namespace

// ---- shape -------------------------------------------------------------------

TEST(Coverage, ReportIsWellFormedJsonWithASchemaVersion) {
    const json rep = report_for("python-expanded");
    EXPECT_EQ(rep.at("rosetta_coverage"), 1);
    EXPECT_TRUE(rep.contains("reflection"));
    EXPECT_TRUE(rep.contains("targets"));
}

TEST(Coverage, AnEmptyLogStillProducesParseableJson) {
    rosetta::coverage::reset();
    const json rep = json::parse(rosetta::coverage::to_json({}));
    EXPECT_EQ(rep.at("rosetta_coverage"), 1);
    EXPECT_TRUE(rep.at("targets").empty());
    EXPECT_TRUE(rep.at("reflection").empty());
}

// ---- the reflection stage: what no backend ever saw ---------------------------

TEST(Coverage, OperatorsAreAttributedToTheReflectionStage) {
    const json rep = report_for("python-expanded");
    bool       found = false;
    for (const auto &k : rep.at("reflection")) {
        if (k.at("class") != "CovThing") {
            continue;
        }
        for (const auto &d : k.at("dropped")) {
            found = found || (d.at("reason") == "no_identifier" &&
                              std::string(d.at("member")).find("operator==") != std::string::npos);
        }
    }
    EXPECT_TRUE(found) << "operator== has no bindable name and must be reported, not just absent";
}

// ---- the backend stage: bound AND skipped -------------------------------------

TEST(Coverage, BoundMembersAreRecordedNotJustSkips) {
    const json node = class_node(report_for("python-expanded"), "python-expanded", "CovThing");
    ASSERT_FALSE(node.is_null());
    EXPECT_TRUE(lists_member(node.at("bound"), "get"))
        << "without the bound side, 'nothing bound' and 'never reached' look identical";
}

TEST(Coverage, AnUnmarshalableSignatureIsSkippedWithAReason) {
    // std::function has no N-API conversion; the report must say so rather than
    // leave the method's absence to be noticed.
    const json node = class_node(report_for("node-expanded"), "node-expanded", "CovThing");
    ASSERT_FALSE(node.is_null());
    const json skip = find_skip(node, "listen");
    ASSERT_FALSE(skip.is_null()) << "the skipped callback method is missing from the report";
    EXPECT_EQ(skip.at("reason"), "unmarshalable_signature");
    EXPECT_FALSE(std::string(skip.at("detail")).empty()) << "a reason slug needs a human sentence";
}

TEST(Coverage, PythonSkipReasonNamesTheOffendingType) {
    // `listen` is NOT the example here: pybind11 has a std::function caster, so
    // it binds. A by-value non-copyable parameter is what pybind cannot take.
    const json node = class_node(report_for("python-expanded"), "python-expanded", "CovThing");
    ASSERT_FALSE(node.is_null());
    EXPECT_TRUE(lists_member(node.at("bound"), "listen")) << "pybind11 marshals std::function";

    const json skip = find_skip(node, "take");
    ASSERT_FALSE(skip.is_null());
    EXPECT_EQ(skip.at("reason"), "unmarshalable_signature");
    EXPECT_NE(std::string(skip.at("detail")).find("CovNoCopy"), std::string::npos)
        << "the detail should name the type that could not be marshalled";
}

// ---- overloads dropped by a name-keyed target ---------------------------------

TEST(Coverage, ADroppedOverloadIsAttributedToTheTargetThatDroppedIt) {
    const json node = class_node(report_for("node-expanded"), "node-expanded", "CovThing");
    ASSERT_FALSE(node.is_null());
    const json skip = find_skip(node, "get");
    ASSERT_FALSE(skip.is_null()) << "the second get() overload must be reported";
    EXPECT_EQ(skip.at("reason"), "overload_not_expressible");
    // The signature is what tells the dropped overload from the one that bound.
    EXPECT_NE(std::string(skip.at("signature")).find("int"), std::string::npos);
    EXPECT_TRUE(lists_member(node.at("bound"), "get")) << "the first overload still binds";
}

TEST(Coverage, ANativeOverloadTargetReportsNoOverloadSkips) {
    const json node = class_node(report_for("python-expanded"), "python-expanded", "CovThing");
    ASSERT_FALSE(node.is_null());
    for (const auto &e : node.at("skipped")) {
        EXPECT_NE(e.at("reason"), "overload_not_expressible")
            << "pybind11 dispatches on argument types — no overload should be dropped";
    }
}

TEST(Coverage, CountsAgreeWithTheListedEntries) {
    const json rep = report_for("node-expanded");
    for (const auto &t : rep.at("targets")) {
        std::size_t bound = 0;
        std::size_t skipped = 0;
        for (const auto &k : t.at("classes")) {
            bound += k.at("bound").size();
            skipped += k.at("skipped").size();
        }
        EXPECT_EQ(t.at("bound"), bound);
        EXPECT_EQ(t.at("skipped"), skipped);
    }
}

// ---- hygiene ------------------------------------------------------------------

TEST(Coverage, ResetClearsTheLogBetweenRuns) {
    report_for("python-expanded");
    rosetta::coverage::reset();
    EXPECT_TRUE(rosetta::coverage::log().bound.empty());
    EXPECT_TRUE(rosetta::coverage::log().skips.empty());
}

TEST(Coverage, QuotesAndBackslashesInADetailStayParseable) {
    rosetta::coverage::reset();
    rosetta::GenClass k;
    k.name = "Q";
    rosetta::GenMethod m;
    m.name = "m";
    rosetta::coverage::note_skip("t", k, m, "reason", "a \"quoted\" \\ back\nslash");
    const json rep = json::parse(rosetta::coverage::to_json({k})); // must not throw
    EXPECT_EQ(rep.at("targets").at(0).at("classes").at(0).at("skipped").at(0).at("detail"),
              "a \"quoted\" \\ back\nslash");
}
