// Copyright (c) fmaerten@gmail.com
// License: MIT

// The extended annotation set (unit, default_value, example, min / max,
// length, regex, requires_, destructive / confirm, expensive, hidden,
// deprecated, since, step, decimals, log_scale, group, order, enabled_if,
// visible_if, widget::plot / progress) — inline AND from a side-car — reaching
// the IR, checked through the Markdown reference, which renders all of them.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

namespace ext_test {

    /// A heater with a thermostat.
    struct Heater {
        [[= rosetta::doc{"Target temperature"}, = rosetta::unit{"degC"},
          = rosetta::min{5.0}, = rosetta::max{30.0}, = rosetta::step{0.5},
          = rosetta::decimals{1}, = rosetta::group{"Control"}, = rosetta::order{1}]]
        double target = 20.0;

        [[= rosetta::length{1, 16}, = rosetta::regex{"[a-z]+"}]]
        std::string room = "kitchen";

        [[= rosetta::hidden{}]] int internalCounter = 0;

        int legacyMode = 0; // annotated from the side-car

        [[= rosetta::default_value{"minutes", 15}, = rosetta::example{"{\"minutes\": 30}"},
          = rosetta::requires_{"minutes > 0", "a boost needs a duration"},
          = rosetta::since{"2.0"}]]
        void boost(int minutes) { (void)minutes; }

        [[= rosetta::confirm{"Turn the heating off?"}]] void shutdown() {}

        [[= rosetta::deprecated{"boost()"}]] void turbo() {}

        void calibrate(double tolerance) { (void)tolerance; } // side-car: default + expensive
    };

} // namespace ext_test

template <>
constexpr std::string_view rosetta::ann_json_source<ext_test::Heater> = R"({
  "legacyMode": { "deprecated": true, "hidden": false, "unit": "1/s", "regex": "\\d+" },
  "calibrate":  { "default": { "tolerance": 0.01 }, "expensive": true }
})";

namespace {
    const std::string md = rosetta::to_markdown<ext_test::Heater>();
} // namespace

TEST(AnnotationsExt, InlineFieldFactsReachTheReference) {
    EXPECT_NE(md.find("unit: degC"), std::string::npos) << md;
    EXPECT_NE(md.find("min: 5"), std::string::npos);
    EXPECT_NE(md.find("max: 30"), std::string::npos);
    EXPECT_NE(md.find("length: 1..16"), std::string::npos);
    EXPECT_NE(md.find("pattern: [a-z]+"), std::string::npos);
}

TEST(AnnotationsExt, HiddenMembersAreNotBound) {
    EXPECT_EQ(md.find("internalCounter"), std::string::npos) << md;
}

TEST(AnnotationsExt, DefaultValueMakesTheParameterDefaulted) {
    EXPECT_NE(md.find("boost(minutes: int = 15)"), std::string::npos) << md;
    // from the side-car, round-trip exact (0.01, not 0.010000000000000002)
    EXPECT_NE(md.find("calibrate(tolerance: double = 0.01)"), std::string::npos) << md;
}

TEST(AnnotationsExt, MethodFactsReachTheReference) {
    EXPECT_NE(md.find("*Requires:* minutes > 0 — a boost needs a duration"), std::string::npos) << md;
    EXPECT_NE(md.find("*Example:* {\"minutes\": 30}"), std::string::npos);
    EXPECT_NE(md.find("*Since:* 2.0"), std::string::npos);
    EXPECT_NE(md.find("*Destructive:* Turn the heating off?"), std::string::npos);
    EXPECT_NE(md.find("*Deprecated:* use boost()"), std::string::npos);
    EXPECT_NE(md.find("*Expensive:*"), std::string::npos);
}

TEST(AnnotationsExt, SideCarStringsAreUnescaped) {
    // "\\d+" in the JSON is the regex \d+
    EXPECT_NE(md.find("pattern: \\d+"), std::string::npos) << md;
    EXPECT_NE(md.find("deprecated)"), std::string::npos);
}
