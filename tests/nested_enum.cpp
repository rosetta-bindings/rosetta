// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for enumerations nested inside a class.
//
// A nested enum's enclosing scope is a CLASS, not a namespace, so
// class_namespace<T>() — which stops at the first non-namespace scope — yields
// "" for it. Reconstructing the C++ spelling as `name_space::name` therefore
// produced the bare identifier ("SolverMode"), which every backend emitted as
// a type name and which does not compile. GenEnum::qualified carries the
// namespaces AND the enclosing classes, and qualified_of() prefers it.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

namespace nens {
    class Solver {
    public:
        enum class Mode { Fast = 0, Exact = 1 };

        Mode mode = Mode::Fast;
    };

    // A namespace-level enum, for contrast: `qualified` must agree with the
    // older `name_space::name` reconstruction here.
    enum class Flat { One = 0, Two = 1 };
} // namespace nens

template <> struct rosetta::binding_info<nens::Solver> {
    static constexpr const char *header = "ne.h";
};
template <> struct rosetta::binding_info<nens::Solver::Mode> {
    static constexpr const char *header = "ne.h";
};
template <> struct rosetta::binding_info<nens::Flat> {
    static constexpr const char *header = "ne.h";
};

// The IR keeps the enclosing class in the qualified spelling, and keeps
// `name_space` namespace-only so `using namespace` stays valid C++.
TEST(NestedEnum, IrQualifiesThroughTheEnclosingClass) {
    const auto c =
        rosetta::gen_detail::make_context<nens::Solver, nens::Solver::Mode, nens::Flat>("netest");
    ASSERT_EQ(c.enums.size(), 2u);

    const auto &nested = c.enums.front();
    EXPECT_EQ(nested.name, "Mode");
    // class_namespace() stops at the first non-namespace scope, so a nested
    // enum reports NO namespace. That is exactly why `name_space::name` was
    // the wrong spelling — and why the field must stay as it is: it feeds
    // `using namespace`, which a class name would make invalid C++.
    EXPECT_EQ(nested.name_space, "");
    EXPECT_EQ(rosetta::gen_detail::qualified_of(nested), "nens::Solver::Mode");
    // The host-language name stays the plain identifier.
    EXPECT_EQ(rosetta::gen_detail::exposed_of(nested), "Mode");

    const auto &flat = c.enums.back();
    EXPECT_EQ(flat.name, "Flat");
    EXPECT_EQ(rosetta::gen_detail::qualified_of(flat), "nens::Flat");
}

// A hand-built GenEnum leaves `qualified` empty; qualified_of() must still fall
// back to the namespace-only reconstruction.
TEST(NestedEnum, QualifiedOfFallsBackWhenQualifiedIsEmpty) {
    rosetta::GenEnum e;
    e.name       = "Mode";
    e.name_space = "nens";
    EXPECT_EQ(rosetta::gen_detail::qualified_of(e), "nens::Mode");

    rosetta::GenEnum global;
    global.name = "Mode";
    EXPECT_EQ(rosetta::gen_detail::qualified_of(global), "Mode");
}

// Every backend that spells the enum as a C++ type must emit the
// class-qualified name. (node-expanded is excluded on purpose: it registers
// enums as name/value pairs via rosetta::make_enum and never names the C++
// type, so it was never affected.)
TEST(NestedEnum, BackendsEmitTheClassQualifiedSpelling) {
    const auto c =
        rosetta::gen_detail::make_context<nens::Solver, nens::Solver::Mode, nens::Flat>("netest");

    for (const char *lang : {"python-expanded", "nanobind-expanded", "wasm-expanded"}) {
        const std::string out = rosetta::backend_registry().at(lang)->render(c);
        EXPECT_NE(out.find("nens::Solver::Mode"), std::string::npos)
            << lang << " lost the enclosing class in the enum spelling";
        // The regression: the bare identifier used as the enum_<> type argument.
        EXPECT_EQ(out.find("enum_<Mode>"), std::string::npos)
            << lang << " emitted the unqualified nested enum type";
    }
}
