// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for out-of-line annotations: metadata supplied through the
// ann_json_source<T> customization point (the same point rosetta_gen bakes a
// side-car JSON into) instead of inline P3394 attributes. See
// <rosetta/annotate.h> and docs/OUT_OF_LINE_ANNOTATIONS.md.
//
// Demonstrates:
//   - a CLEAN class (no inline annotations) annotated entirely from JSON
//   - inline + JSON annotations on one member being CONCATENATED
//   - a class with no source being un-annotated (the primary template)
//
// The JSON here is given as a raw string literal so the test is self-contained;
// rosetta_gen produces the identical specialization by baking a .json file.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <cstddef>
#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

// ---- clean class: zero inline annotations, all metadata from JSON ----------
struct Widget {
    std::string title;
    int         count = 0;
    int         id    = 0;
    std::string mode;
};
template <>
constexpr std::string_view rosetta::ann_json_source<Widget> = R"({
  "title": { "doc": "The widget title" },
  "count": { "doc": "Visible items", "range": [0, 100] },
  "id":    { "readonly": true },
  "mode":  { "combobox": ["fast", "slow"] }
})";

// ---- mixed: inline doc + JSON range on the same member ---------------------
struct Mixed {
    [[= rosetta::doc{"inline summary"}]] int value = 0;
};
template <>
constexpr std::string_view rosetta::ann_json_source<Mixed> = R"({ "value": { "range": [1, 9] } })";

// ---- no source: stays un-annotated (primary ann_json_source<Plain>) --------
struct Plain {
    int x = 0;
};

// ---------------------------------------------------------------------------

TEST(AnnotateJson, CleanClassPicksUpAllKindsFromJson) {
    const std::string md = rosetta::to_markdown<Widget>();
    EXPECT_NE(md.find("The widget title"), std::string::npos); // doc
    EXPECT_NE(md.find("0..100"), std::string::npos);           // range
    EXPECT_NE(md.find("readonly"), std::string::npos);         // readonly
    EXPECT_NE(md.find("fast"), std::string::npos);             // combobox
    EXPECT_NE(md.find("slow"), std::string::npos);
}

TEST(AnnotateJson, InlineAndJsonAreConcatenated) {
    const std::string md = rosetta::to_markdown<Mixed>();
    EXPECT_NE(md.find("inline summary"), std::string::npos); // inline kept
    EXPECT_NE(md.find("1..9"), std::string::npos);           // JSON merged in
}

TEST(AnnotateJson, NoSourceIsUnannotated) {
    const std::string md = rosetta::to_markdown<Plain>();
    EXPECT_EQ(md.find("range:"), std::string::npos);
    EXPECT_EQ(md.find("readonly"), std::string::npos);
    EXPECT_EQ(md.find("choices:"), std::string::npos);
    EXPECT_TRUE(rosetta::ann_json_source<Plain>.empty());
}
