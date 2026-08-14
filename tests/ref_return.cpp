// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for reference returns in the Python-family backends.
//
// pybind11 and nanobind both COPY an lvalue-reference return under their
// automatic policy. For a fluent API returning `*this` that is silently wrong
// and silently expensive: the handle Python gets back is a copy, so mutations
// through it are lost, and every call copies the whole receiver — measured at
// ~14 µs/call on a real binding, growing with the object, which turns an
// accumulate-in-a-loop into quadratic work. A non-const lvalue-reference return
// of a BOUND class therefore binds with reference_internal.
//
// Const references keep copying on purpose: Python cannot express constness, so
// binding one by reference would hand scripts a mutable view of what the C++ API
// declared read-only.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

namespace fl {

    class Knob {
    public:
        Knob() = default;
        double v = 0;
    };

    class Builder {
    public:
        Builder() = default;

        // The fluent shape: returns *this by non-const reference.
        Builder &addPoint(double x) {
            sum_ += x;
            return *this;
        }

        // A non-const reference to a bound member — same rule.
        Knob &knob() { return knob_; }

        // Const reference: keeps copying, deliberately.
        const Knob &peek() const { return knob_; }

        // By value: nothing to do.
        double total() const { return sum_; }

    private:
        double sum_ = 0;
        Knob   knob_;
    };

} // namespace fl

template <> struct rosetta::binding_info<fl::Builder> {
    static constexpr const char *header = "Builder.h";
};
template <> struct rosetta::binding_info<fl::Knob> {
    static constexpr const char *header = "Builder.h";
};

namespace {

    // The emitted line for `name`, whichever backend produced it.
    std::string line_for(const std::string &src, const std::string &name) {
        const std::string     needle = "\"" + name + "\"";
        std::string::size_type at    = src.find(needle);
        if (at == std::string::npos) {
            return {};
        }
        const std::string::size_type from = src.rfind('\n', at);
        const std::string::size_type to   = src.find('\n', at);
        return src.substr(from + 1, to - from - 1);
    }

} // namespace

// The IR already knew the shape (GenMethod::ret_is_ref); this pins that the
// exact spelling is what tells a const reference from a mutable one.
TEST(RefReturn, IrRecordsTheReferenceAndItsConstness) {
    const auto c = rosetta::gen_detail::make_context<fl::Builder, fl::Knob>("rtest");
    ASSERT_FALSE(c.classes.empty());
    for (const auto &m : c.classes.front().methods) {
        if (m.name == "addPoint" || m.name == "knob") {
            EXPECT_TRUE(m.ret_is_ref) << m.name;
            EXPECT_NE(m.ret_cpp.rfind("const", 0), 0u) << m.name;
        }
        if (m.name == "peek") {
            EXPECT_TRUE(m.ret_is_ref);
            EXPECT_EQ(m.ret_cpp.rfind("const", 0), 0u) << "peek() returns a const reference";
        }
        if (m.name == "total") {
            EXPECT_FALSE(m.ret_is_ref);
        }
    }
}

TEST(RefReturn, PybindBindsMutableReferencesWithoutCopying) {
    const auto        c = rosetta::gen_detail::make_context<fl::Builder, fl::Knob>("rtest");
    const std::string out =
        rosetta::backend_registry().at("python")->render(c);

    EXPECT_NE(line_for(out, "addPoint").find("return_value_policy::reference_internal"),
              std::string::npos)
        << "the fluent return would be copied on every call: " << line_for(out, "addPoint");
    EXPECT_NE(line_for(out, "knob").find("return_value_policy::reference_internal"),
              std::string::npos)
        << line_for(out, "knob");
    EXPECT_EQ(line_for(out, "peek").find("return_value_policy"), std::string::npos)
        << "a const reference must keep copying: " << line_for(out, "peek");
    EXPECT_EQ(line_for(out, "total").find("return_value_policy"), std::string::npos)
        << line_for(out, "total");
}

TEST(RefReturn, NanobindBindsMutableReferencesWithoutCopying) {
    const auto        c = rosetta::gen_detail::make_context<fl::Builder, fl::Knob>("rtest");
    const std::string out =
        rosetta::backend_registry().at("nanobind")->render(c);

    EXPECT_NE(line_for(out, "addPoint").find("rv_policy::reference_internal"), std::string::npos)
        << line_for(out, "addPoint");
    EXPECT_NE(line_for(out, "knob").find("rv_policy::reference_internal"), std::string::npos)
        << line_for(out, "knob");
    EXPECT_EQ(line_for(out, "peek").find("rv_policy"), std::string::npos)
        << line_for(out, "peek");
}
