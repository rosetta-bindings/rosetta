// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for OVERLOAD SUPPORT in the reflection walk.
//
// The walk used to dedup member functions by identifier: of `at(int)` and
// `at(int, int)` exactly one entry reached the IR and the other vanished with no
// diagnostic. It now dedups by (identifier, function type), so an overload set
// arrives whole, and each backend applies the policy its target language can
// honour (see member_object.cpp for the per-backend emission).
//
// The three rules that are easy to get wrong, and are pinned here:
//
//   * a class's own overloads all survive;
//   * a derived declaration of a name HIDES every base overload of that name,
//     matching C++ name lookup without a `using Base::f` — and each hidden one
//     is recorded as a drop rather than silently disappearing;
//   * a diamond-shared base member is still emitted exactly once.
//
// Verifies the IR (describe / make_context), not a live build.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <algorithm>
#include <string>
#include <vector>

// ---- fixtures ---------------------------------------------------------------

struct OvBasic {
    int  at() const { return 0; }
    int  at(int i) const { return i; }
    int  at(int i, int j) const { return i + j; }
    void plain() {}
};

// Name hiding: Derived::f(double) hides BOTH Base::f overloads.
struct OvBase {
    int f(int) const { return 1; }
    int f(int, int) const { return 2; }
    int inherited() const { return 3; }
};
struct OvDerived : OvBase {
    int f(double) const { return 4; }
};

// Diamond: OvJoin reaches OvRoot twice; OvRoot::shared() must appear once.
struct OvRoot {
    int shared() const { return 0; }
};
struct OvLeft : virtual OvRoot {};
struct OvRight : virtual OvRoot {};
struct OvJoin : OvLeft, OvRight {};

// Members the walk cannot hand to any backend, each for a different reason.
struct OvDropped {
    int  ok() const { return 0; }
    bool operator==(const OvDropped &) const { return true; }
    int  operator[](int) const { return 0; }
    template <typename U> void tmpl(U) {}
};

template <> struct rosetta::binding_info<OvBasic> {
    static constexpr const char *header = "ov.h";
};
template <> struct rosetta::binding_info<OvDerived> {
    static constexpr const char *header = "ov.h";
};
template <> struct rosetta::binding_info<OvJoin> {
    static constexpr const char *header = "ov.h";
};
template <> struct rosetta::binding_info<OvDropped> {
    static constexpr const char *header = "ov.h";
};

namespace {

    std::vector<const rosetta::GenMethod *> named(const rosetta::GenClass &k, const char *name) {
        std::vector<const rosetta::GenMethod *> out;
        for (const auto &m : k.methods) {
            if (m.name == name) {
                out.push_back(&m);
            }
        }
        return out;
    }

    bool has_drop(const rosetta::GenClass &k, const char *member, const char *reason) {
        return std::any_of(k.dropped.begin(), k.dropped.end(), [&](const rosetta::GenDrop &d) {
            return d.member.find(member) != std::string::npos && d.reason == reason;
        });
    }

} // namespace

// ---- a class's own overload set ---------------------------------------------

TEST(Overload, EveryOverloadReachesTheIr) {
    const auto k  = rosetta::gen_detail::describe<OvBasic>();
    const auto at = named(k, "at");
    ASSERT_EQ(at.size(), 3u) << "the walk dropped part of the overload set";
    // Declaration order is preserved, and arity tells them apart.
    EXPECT_EQ(at[0]->params.size(), 0u);
    EXPECT_EQ(at[1]->params.size(), 1u);
    EXPECT_EQ(at[2]->params.size(), 2u);
}

TEST(Overload, OrdinalsNumberTheSetInDeclarationOrder) {
    const auto k  = rosetta::gen_detail::describe<OvBasic>();
    const auto at = named(k, "at");
    ASSERT_EQ(at.size(), 3u);
    for (std::size_t i = 0; i < at.size(); ++i) {
        EXPECT_EQ(at[i]->overload_index, i);
        EXPECT_EQ(at[i]->overload_count, 3u);
        EXPECT_TRUE(at[i]->is_overloaded);
    }
}

TEST(Overload, APlainMethodIsNotMarkedOverloaded) {
    const auto k     = rosetta::gen_detail::describe<OvBasic>();
    const auto plain = named(k, "plain");
    ASSERT_EQ(plain.size(), 1u);
    EXPECT_FALSE(plain[0]->is_overloaded);
    EXPECT_EQ(plain[0]->overload_index, 0u);
    EXPECT_EQ(plain[0]->overload_count, 1u);
}

// ---- name hiding across bases -----------------------------------------------

TEST(Overload, DerivedDeclarationHidesTheWholeBaseSet) {
    const auto k = rosetta::gen_detail::describe<OvDerived>();
    const auto f = named(k, "f");
    ASSERT_EQ(f.size(), 1u) << "base overloads of a hidden name must not bind";
    EXPECT_EQ(f[0]->params.size(), 1u);
    EXPECT_EQ(f[0]->params[0].type.spelling, "double") << "the derived f(double) is the survivor";
    EXPECT_EQ(f[0]->overload_count, 1u);
    // And NOT flagged is_overloaded: that asks whether the declaring class
    // overloads the name, and OvDerived declares `f` exactly once. Name lookup
    // for `&OvDerived::f` finds only that one — the hidden base declarations do
    // not make it ambiguous — so no disambiguating cast is needed either.
    EXPECT_FALSE(f[0]->is_overloaded);
}

TEST(Overload, HiddenBaseOverloadsAreRecordedNotSilentlyDropped) {
    const auto k = rosetta::gen_detail::describe<OvDerived>();
    EXPECT_TRUE(has_drop(k, "f", "hidden_by_derived"))
        << "a hidden base overload must appear in the coverage drops";
    std::size_t hidden = 0;
    for (const auto &d : k.dropped) {
        hidden += (d.reason == "hidden_by_derived") ? 1 : 0;
    }
    EXPECT_EQ(hidden, 2u) << "both OvBase::f overloads are hidden by OvDerived::f";
}

TEST(Overload, AnUnhiddenBaseMethodStillBinds) {
    const auto k = rosetta::gen_detail::describe<OvDerived>();
    EXPECT_EQ(named(k, "inherited").size(), 1u);
}

// ---- diamond -----------------------------------------------------------------

TEST(Overload, DiamondSharedBaseIsEmittedOnce) {
    const auto k = rosetta::gen_detail::describe<OvJoin>();
    EXPECT_EQ(named(k, "shared").size(), 1u)
        << "a virtual base reached along two paths must not double up";
}

// ---- what the walk cannot bind at all ---------------------------------------

TEST(Overload, OperatorsAndTemplatesAreRecordedAsDrops) {
    const auto k = rosetta::gen_detail::describe<OvDropped>();
    EXPECT_EQ(named(k, "ok").size(), 1u);
    EXPECT_TRUE(has_drop(k, "operator==", "no_identifier"));
    EXPECT_TRUE(has_drop(k, "operator[]", "no_identifier"));
    EXPECT_TRUE(has_drop(k, "tmpl", "function_template"));
}

TEST(Overload, DropsCarryASignatureToTellOverloadsApart) {
    const auto k = rosetta::gen_detail::describe<OvDerived>();
    std::vector<std::string> sigs;
    for (const auto &d : k.dropped) {
        if (d.reason == "hidden_by_derived") {
            sigs.push_back(d.signature);
        }
    }
    ASSERT_EQ(sigs.size(), 2u);
    EXPECT_NE(sigs[0], sigs[1]) << "two hidden overloads must be distinguishable in the report";
}
