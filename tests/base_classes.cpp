// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for REGISTERING the inheritance relationship in the
// emitted bindings — GenClass::bases reaching py::class_<T, Base> and
// nb::class_<T, Base>.
//
// This is what makes a derived instance acceptable where the base is expected.
// Without it each class is an unrelated host-language type, and passing a
// Derived to a method taking `Base *` fails at the boundary ("incompatible
// function arguments"), even though the C++ call would be trivially valid.
//
// tests/inheritance.cpp covers the other half — walk<T>() flattening inherited
// MEMBERS onto the derived binding. Flattening and registration are
// independent: the members can all be there and the conversion still fail.
//
// The nanobind cases also pin the single-base rule: nb::class_<T, Ts...>
// extracts one `Base` and static_asserts that no argument is left over, so a
// second bound base must be dropped by the backend rather than emitted into
// code that cannot compile. pybind11 has no such limit.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

namespace bcns {
    struct Base {
        int base_val = 0;

        int  probe() const { return base_val; }
        virtual ~Base() = default;
    };

    // Single inheritance — the ordinary case.
    struct Derived : Base {
        double extra = 0.0;
    };

    // A second, independent base. `Both` inherits from one bound base and one
    // that the context may or may not list.
    struct Other {
        int other_val = 0;
    };

    struct Both : Base, Other {
        int both_val = 0;
    };
} // namespace bcns

template <> struct rosetta::binding_info<bcns::Base> {
    static constexpr const char *header = "bc.h";
};
template <> struct rosetta::binding_info<bcns::Derived> {
    static constexpr const char *header = "bc.h";
};
template <> struct rosetta::binding_info<bcns::Other> {
    static constexpr const char *header = "bc.h";
};
template <> struct rosetta::binding_info<bcns::Both> {
    static constexpr const char *header = "bc.h";
};

static std::string render(const char *lang, const rosetta::GenContext &c) {
    return rosetta::backend_registry().at(lang)->render(c);
}

// Base listed BEFORE the derived: nanobind needs the base registered by the
// time the derived class declares it.
static rosetta::GenContext ctx_base_and_derived() {
    return rosetta::gen_detail::make_context<bcns::Base, bcns::Derived>("bctest");
}

// ---- the IR carries the bases ----------------------------------------------

TEST(BaseClasses, ContextRecordsDirectPublicBases) {
    const auto c = ctx_base_and_derived();
    ASSERT_EQ(c.classes.size(), 2u);
    EXPECT_TRUE(c.classes[0].bases.empty()); // Base
    ASSERT_EQ(c.classes[1].bases.size(), 1u);
    EXPECT_EQ(c.classes[1].bases[0], "bcns::Base"); // qualified spelling
}

// ---- nanobind --------------------------------------------------------------

TEST(BaseClasses, NanobindRegistersTheBase) {
    const std::string s = render("nanobind", ctx_base_and_derived());
    EXPECT_NE(s.find("nb::class_<bcns::Derived, bcns::Base>(m, \"Derived\")"), std::string::npos);
}

TEST(BaseClasses, NanobindLeavesARootClassAlone) {
    const std::string s = render("nanobind", ctx_base_and_derived());
    // Base has no base of its own: no trailing template argument.
    EXPECT_NE(s.find("nb::class_<bcns::Base>(m, \"Base\")"), std::string::npos);
}

TEST(BaseClasses, NanobindDropsAnUnboundBase) {
    // Derived alone: bcns::Base is not in the module, and nanobind resolves a
    // base by looking the type up there. Naming it would break the import.
    const auto        c = rosetta::gen_detail::make_context<bcns::Derived>("bctest");
    const std::string s = render("nanobind", c);
    EXPECT_NE(s.find("nb::class_<bcns::Derived>(m, \"Derived\")"), std::string::npos);
    EXPECT_EQ(s.find("bcns::Derived, bcns::Base"), std::string::npos);
}

TEST(BaseClasses, NanobindKeepsOnlyOneOfSeveralBoundBases) {
    // Both : Base, Other — with both bases bound. nanobind has no multiple
    // inheritance, so exactly one relationship is registered (the first).
    const auto c = rosetta::gen_detail::make_context<bcns::Base, bcns::Other, bcns::Both>("bctest");
    const std::string s = render("nanobind", c);
    EXPECT_NE(s.find("nb::class_<bcns::Both, bcns::Base>(m, \"Both\")"), std::string::npos);
    EXPECT_EQ(s.find("bcns::Base, bcns::Other"), std::string::npos);
}

// ---- pybind11 keeps multiple inheritance (no regression) -------------------

TEST(BaseClasses, PybindRegistersEveryBoundBase) {
    const auto c = rosetta::gen_detail::make_context<bcns::Base, bcns::Other, bcns::Both>("bctest");
    const std::string s = render("python", c);
    // py::class_ takes as many bases as the C++ type has.
    EXPECT_NE(s.find("py::class_<bcns::Both, bcns::Base, bcns::Other>"), std::string::npos);
}
