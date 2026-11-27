// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for the manifest "signals" field (rosetta/signal.h): the
// loader's defaults and its validation. The loader itself, compiled directly,
// with no reflection involved.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <manifest.h>
#include <stdexcept>
#include <string>

namespace {

    struct Tree {
        fs::path root;

        Tree() {
            // Named after the test: ctest runs each test as its own process, in parallel.
            static int n = 0;
            root = fs::temp_directory_path() /
                   ("rosetta_manifest_signals_" +
                    std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) +
                    "_" + std::to_string(++n));
            fs::remove_all(root);
            fs::create_directories(root);
            std::ofstream(root / "Point.h") << "struct Point {};\n";
        }
        ~Tree() {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        Manifest load_with(const std::string &signals) const {
            const fs::path p = root / "manifest.json";
            std::ofstream(p) << "{\n"
                             << "  \"user_include\": \".\",\n"
                             << "  \"rosetta_include\": \".\",\n"
                             << "  \"targets\": [\"wasm\"],\n"
                             << "  \"signals\": " << signals << ",\n"
                             << "  \"classes\": [{\"header\": \"Point.h\"}]\n}\n";
            return load(p);
        }
    };

} // namespace

TEST(ManifestSignals, ABareTemplateNameTakesTheDefaults) {
    const Manifest m = Tree().load_with(R"json(["boost::signals2::signal"])json");
    ASSERT_EQ(m.signals.size(), 1u);
    EXPECT_EQ(m.signals[0].template_name, "boost::signals2::signal");
    EXPECT_EQ(m.signals[0].connect, "$signal.connect($fn)");
    EXPECT_EQ(m.signals[0].method, "on{Field}");
    EXPECT_TRUE(m.signals[0].returns.empty());
}

TEST(ManifestSignals, TheObjectFormSetsEveryKey) {
    const Manifest m = Tree().load_with(R"json([{
        "template": "veyra::Signal", "header": "veyra/core/signal.h",
        "connect": "$signal.connect(&$self, $fn)", "returns": "veyra::Connection",
        "method": "{field}Connect"}])json");
    ASSERT_EQ(m.signals.size(), 1u);
    const auto &s = m.signals[0];
    EXPECT_EQ(s.template_name, "veyra::Signal");
    EXPECT_EQ(s.header, "veyra/core/signal.h");
    EXPECT_EQ(s.connect, "$signal.connect(&$self, $fn)");
    EXPECT_EQ(s.returns, "veyra::Connection");
    EXPECT_EQ(s.method, "{field}Connect");
}

TEST(ManifestSignals, RejectsWhatCannotWork) {
    const Tree t;
    EXPECT_THROW(t.load_with(R"json({"template": "S"})json"), std::runtime_error);       // not an array
    EXPECT_THROW(t.load_with(R"json([{"connect": "$signal.connect($fn)"}])json"), std::runtime_error);
    EXPECT_THROW(t.load_with(R"json([{"template": "S", "connect": "$signal.connect()"}])json"),
                 std::runtime_error);                                            // no $fn
    EXPECT_THROW(t.load_with(R"json([{"template": "S", "method": "connect"}])json"),
                 std::runtime_error);                                            // one name for all
    EXPECT_THROW(t.load_with(R"json(["S", "S"])json"), std::runtime_error);              // duplicate
}
