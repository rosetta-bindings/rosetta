// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for the PUBLIC-API SNAPSHOT (<rosetta/api.h>, written by
// generate() as <out_dir>/api.json) and the diff between two of them
// (<rosetta/api_diff.h>, `rosetta_gen --api-diff`).
//
// Two halves:
//   * end to end — two versions of the same small library (namespaces v1 / v2,
//     stripped from the snapshot so the classes compare as one) are reflected,
//     snapshotted and diffed; the expected breaking / compatible changes must
//     all be found, and nothing else;
//   * the classification rules, one by one, on hand-written snapshots.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <rosetta/api_diff.h>
#include <rosetta/generate.h>
#include <filesystem>
#include <fstream>
#include <string>

using json = nlohmann::json;
using rosetta::api::Change;
using rosetta::api::severity;

// ---- two releases of one library ---------------------------------------------

namespace v1 {
    enum class Mode { fast = 0, exact = 1, legacy = 2 };

    struct Mesh {
        [[= rosetta::range{0, 100}]] int quality = 50;
        double                           scale   = 1;
        int                              count() const { return 0; }
        void                             remesh(double edge) { (void)edge; }
        void                             smooth(int iterations) { (void)iterations; }
        double                           area() const { return 0; }
    };
} // namespace v1

namespace v2 {
    enum class Mode { fast = 0, exact = 2, precise = 3 };

    struct Mesh {
        [[= rosetta::range{10, 100}]] int quality = 50;   // range narrowed
        double                            scale   = 1;
        int                               count() const { return 0; }
        void remesh(double edge, int passes = 1) { (void)edge, (void)passes; } // compatible
        void smooth(int iterations, bool keep) { (void)iterations, (void)keep; } // breaking
        double surface() const { return 0; }                                   // area renamed
        void   clear() {}                                                      // new
    };
} // namespace v2

template <> struct rosetta::binding_info<v1::Mesh> {
    static constexpr const char *header = "mesh.h";
};
template <> struct rosetta::binding_info<v2::Mesh> {
    static constexpr const char *header = "mesh.h";
};
template <> struct rosetta::binding_info<v1::Mode> {
    static constexpr const char *header = "mesh.h";
};
template <> struct rosetta::binding_info<v2::Mode> {
    static constexpr const char *header = "mesh.h";
};

namespace {

    // The snapshot of one release, its namespace erased so v1::Mesh and
    // v2::Mesh are the same class to the differ.
    template <typename... Ts> json snapshot(const char *ns, const char *version) {
        const auto  c = rosetta::gen_detail::make_context<Ts...>("lib");
        std::string s = rosetta::api::to_json(c.classes, c.enums, c.functions, version);
        const std::string prefix = std::string(ns) + "::";
        for (std::size_t at; (at = s.find(prefix)) != std::string::npos;) {
            s.erase(at, prefix.size());
        }
        return json::parse(s);
    }

    const Change *find(const std::vector<Change> &cs, const std::string &kind,
                       const std::string &member) {
        for (const Change &c : cs) {
            if (c.kind == kind && c.member == member) {
                return &c;
            }
        }
        return nullptr;
    }

    // A minimal snapshot with one class, for the rule-by-rule tests.
    json with_class(json cls) {
        cls.emplace("expose", cls.value("name", std::string()));
        return json{{"rosetta_api", 1},
                    {"version", ""},
                    {"classes", json::array({cls})},
                    {"enums", json::array()},
                    {"functions", json::array()}};
    }

    json method(const char *name, json params, const char *ret = "void") {
        return json{{"name", name}, {"returns", ret}, {"params", params}};
    }

    json param(const char *name, const char *type) { return json{{"name", name}, {"type", type}}; }

    json param(const char *name, const char *type, const char *def) {
        return json{{"name", name}, {"type", type}, {"default", def}};
    }

    std::vector<Change> diff_methods(json before, json after) {
        return rosetta::api::diff(
            with_class(json{{"name", "K"}, {"methods", std::move(before)}}),
            with_class(json{{"name", "K"}, {"methods", std::move(after)}}));
    }

} // namespace

// ---- the snapshot ------------------------------------------------------------

TEST(ApiSnapshot, IsWellFormedJsonWithASchemaVersion) {
    const json s = snapshot<v1::Mesh, v1::Mode>("v1", "1.0");
    EXPECT_EQ(s.at("rosetta_api"), 1);
    EXPECT_EQ(s.at("version"), "1.0");
    ASSERT_EQ(s.at("classes").size(), 1u);
    ASSERT_EQ(s.at("enums").size(), 1u);
    EXPECT_TRUE(s.at("functions").empty());
}

TEST(ApiSnapshot, CarriesSignaturesRangesAndEnumerators) {
    const json  s = snapshot<v1::Mesh, v1::Mode>("v1", "1.0");
    const json &k = s.at("classes")[0];
    EXPECT_EQ(k.at("name"), "Mesh");

    bool saw_range = false;
    for (const auto &f : k.at("fields")) {
        if (f.at("name") == "quality") {
            saw_range = f.contains("range") && f["range"]["min"] == 0 && f["range"]["max"] == 100;
        }
    }
    EXPECT_TRUE(saw_range) << "a range{} annotation is part of the contract and must be dumped";

    bool saw_remesh = false;
    for (const auto &m : k.at("methods")) {
        if (m.at("name") == "remesh") {
            saw_remesh = m.at("params").size() == 1 && m["params"][0]["name"] == "edge";
        }
    }
    EXPECT_TRUE(saw_remesh);

    EXPECT_EQ(s.at("enums")[0].at("values").size(), 3u);
}

TEST(ApiSnapshot, GenerateWritesItNextToTheCoverageReport) {
    namespace fs       = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "rosetta_api_snapshot";
    fs::remove_all(dir);

    rosetta::GenerateOptions opt;
    opt.out_dir         = dir;
    opt.rosetta_include = "/rosetta/include";
    opt.targets         = {{"python", "apitest"}};
    opt.version         = "3.1.4";
    rosetta::generate<v1::Mesh, v1::Mode>(opt);

    std::ifstream in(dir / "api.json");
    ASSERT_TRUE(in) << "generate() must always write api.json";
    const json s = json::parse(in);
    EXPECT_EQ(s.at("version"), "3.1.4");
    EXPECT_EQ(s.at("classes").size(), 1u);
    EXPECT_TRUE(fs::exists(dir / "coverage.json"));
    fs::remove_all(dir);
}

TEST(ApiSnapshot, AnEmptyIrStillProducesParseableJson) {
    const json s = json::parse(rosetta::api::to_json({}, {}, {}, ""));
    EXPECT_TRUE(s.at("classes").empty());
}

// ---- end to end: v1 -> v2 ----------------------------------------------------

TEST(ApiDiff, FindsEveryChangeBetweenTwoReleases) {
    const auto cs = rosetta::api::diff(snapshot<v1::Mesh, v1::Mode>("v1", "1.0"),
                                       snapshot<v2::Mesh, v2::Mode>("v2", "2.0"));

    const Change *narrowed = find(cs, "range_narrowed", "quality");
    ASSERT_NE(narrowed, nullptr);
    EXPECT_EQ(narrowed->level, severity::breaking);

    const Change *appended = find(cs, "params_appended", "remesh");
    ASSERT_NE(appended, nullptr) << "a trailing defaulted parameter keeps old calls working";
    EXPECT_EQ(appended->level, severity::compatible);

    const Change *smooth = find(cs, "signature_changed", "smooth");
    ASSERT_NE(smooth, nullptr);
    EXPECT_EQ(smooth->level, severity::breaking);

    const Change *renamed = find(cs, "method_renamed", "area");
    ASSERT_NE(renamed, nullptr) << "area() and surface() share a signature: one rename";
    EXPECT_NE(renamed->detail.find("surface"), std::string::npos);
    EXPECT_EQ(find(cs, "method_removed", "area"), nullptr);
    EXPECT_EQ(find(cs, "method_added", "surface"), nullptr);

    EXPECT_NE(find(cs, "method_added", "clear"), nullptr);

    EXPECT_NE(find(cs, "enumerator_value_changed", "exact"), nullptr);
    EXPECT_NE(find(cs, "enumerator_removed", "legacy"), nullptr);
    EXPECT_NE(find(cs, "enumerator_added", "precise"), nullptr);

    // Nothing reported for what did not change.
    for (const Change &c : cs) {
        EXPECT_NE(c.member, "count") << c.kind;
        EXPECT_NE(c.member, "scale") << c.kind;
    }
}

TEST(ApiDiff, ASnapshotDiffedWithItselfIsEmpty) {
    const json s = snapshot<v1::Mesh, v1::Mode>("v1", "1.0");
    EXPECT_TRUE(rosetta::api::diff(s, s).empty());
}

// ---- rules -------------------------------------------------------------------

TEST(ApiDiffRules, RemovedClassIsBreakingAndNewClassIsCompatible) {
    json a = with_class(json{{"name", "Old"}, {"fields", json::array({{{"name", "x"}, {"type", "int"}}})}});
    json b = with_class(json{{"name", "New"}, {"fields", json::array({{{"name", "y"}, {"type", "int"}}})}});
    const auto cs = rosetta::api::diff(a, b);
    ASSERT_EQ(cs.size(), 2u);
    EXPECT_EQ(cs[0].kind, "class_removed");
    EXPECT_EQ(cs[0].level, severity::breaking);
    EXPECT_EQ(cs[1].kind, "class_added");
    EXPECT_EQ(cs[1].level, severity::compatible);
}

TEST(ApiDiffRules, AClassWithTheSameMembersUnderANewNameIsARename) {
    const json fields = json::array({{{"name", "x"}, {"type", "int"}}});
    const auto cs     = rosetta::api::diff(with_class(json{{"name", "Old"}, {"fields", fields}}),
                                           with_class(json{{"name", "New"}, {"fields", fields}}));
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0].kind, "class_renamed");
    EXPECT_EQ(cs[0].level, severity::breaking);
}

TEST(ApiDiffRules, ARenamedParameterIsAWarning) {
    const auto cs = diff_methods(json::array({method("f", json::array({param("a", "int")}))}),
                                 json::array({method("f", json::array({param("b", "int")}))}));
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0].kind, "param_renamed");
    EXPECT_EQ(cs[0].level, severity::warning);
}

TEST(ApiDiffRules, DefaultsLostAddedAndChanged) {
    auto cs = diff_methods(json::array({method("f", json::array({param("a", "int", "1")}))}),
                           json::array({method("f", json::array({param("a", "int")}))}));
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0].kind, "default_removed");
    EXPECT_EQ(cs[0].level, severity::breaking);

    cs = diff_methods(json::array({method("f", json::array({param("a", "int")}))}),
                      json::array({method("f", json::array({param("a", "int", "1")}))}));
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0].level, severity::compatible);

    cs = diff_methods(json::array({method("f", json::array({param("a", "int", "1")}))}),
                      json::array({method("f", json::array({param("a", "int", "2")}))}));
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0].kind, "default_changed");
    EXPECT_EQ(cs[0].level, severity::warning);
}

TEST(ApiDiffRules, ReturnTypeChangeIsBreaking) {
    const auto cs = diff_methods(json::array({method("f", json::array(), "int")}),
                                 json::array({method("f", json::array(), "double")}));
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0].kind, "return_type_changed");
}

TEST(ApiDiffRules, OverloadsAreMatchedByParameterTypes) {
    const auto cs = diff_methods(
        json::array({method("at", json::array({param("i", "int")})),
                     method("at", json::array({param("i", "int"), param("j", "int")}))}),
        json::array({method("at", json::array({param("i", "int")})),
                     method("at", json::array({param("s", "const std::string &")}))}));
    // (int) is unchanged; (int, int) and (string) are the only leftovers, one
    // on each side, so they are read as one changed signature.
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0].kind, "signature_changed");
}

TEST(ApiDiffRules, FieldRulesReadonlyRenameAndChoices) {
    const auto f = [](const char *n, const char *t) { return json{{"name", n}, {"type", t}}; };

    json ro = f("x", "int");
    ro["readonly"] = true;
    auto cs = rosetta::api::diff(with_class(json{{"name", "K"}, {"fields", json::array({f("x", "int")})}}),
                                 with_class(json{{"name", "K"}, {"fields", json::array({ro})}}));
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0].kind, "became_readonly");

    cs = rosetta::api::diff(with_class(json{{"name", "K"}, {"fields", json::array({f("w", "double"), f("n", "int")})}}),
                            with_class(json{{"name", "K"}, {"fields", json::array({f("width", "double"), f("n", "int")})}}));
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0].kind, "field_renamed");

    json c1 = f("algo", "std::string"), c2 = c1;
    c1["choices"] = {"a", "b"};
    c2["choices"] = {"a", "c"};
    cs = rosetta::api::diff(with_class(json{{"name", "K"}, {"fields", json::array({c1})}}),
                            with_class(json{{"name", "K"}, {"fields", json::array({c2})}}));
    ASSERT_EQ(cs.size(), 2u);
    EXPECT_EQ(cs[0].kind, "choice_removed");
    EXPECT_EQ(cs[1].kind, "choice_added");
}

TEST(ApiDiffRules, ExposedNameChangeIsBreaking) {
    json a = with_class(json{{"name", "ns::K"}});
    json b = a;
    b["classes"][0]["expose"] = "Kay";
    const auto cs = rosetta::api::diff(a, b);
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(cs[0].kind, "exposed_name_changed");
}

TEST(ApiDiffRules, NotASnapshotThrows) {
    EXPECT_THROW(rosetta::api::diff(json::object(), json::object()), std::runtime_error);
}

TEST(ApiDiffReport, JsonAndTextAgreeOnTheCounts) {
    const auto cs = rosetta::api::diff(snapshot<v1::Mesh, v1::Mode>("v1", "1.0"),
                                       snapshot<v2::Mesh, v2::Mode>("v2", "2.0"));
    const json r  = rosetta::api::report_json(cs, "1.0", "2.0");
    EXPECT_EQ(r.at("breaking").get<std::size_t>() + r.at("warning").get<std::size_t>() +
                  r.at("compatible").get<std::size_t>(),
              cs.size());
    const std::string text = rosetta::api::report_text(cs, "1.0", "2.0");
    EXPECT_NE(text.find("1.0 -> 2.0"), std::string::npos);
    EXPECT_NE(text.find("BREAKING"), std::string::npos);
}
