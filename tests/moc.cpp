// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for the moc-less signals / slots / properties built on top
// of C++26 reflection (P2996) + annotations (P3394).
//
// Covers:
//   - Signal<...> data members (recognized by type — no annotation)
//   - [[= slot]]-tagged methods
//   - [[= property{"name", "notifySig"}]]-tagged data members
//   - connect<"sig","slot">(sender, receiver) and equality-gated set<> + NOTIFY
//   - Signal connect / disconnect / disconnect_all, re-entrancy
//   - ScopedConnection RAII (scope-exit, reset, release, move)
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/mini_moc.h>
#include <string>
#include <utility>

namespace moc = rosetta::moc;

// ----------------------------------------------------------

class Person {
public:
    moc::Signal<std::string const &> nameChanged; // a Signal<...> member IS a signal

    moc::Signal<int> ageChanged;

    [[= moc::property{"name", "nameChanged"}]] std::string m_name;

    [[= moc::property{"age", "ageChanged"}]] int m_age = 0;

    [[= moc::property{"id"}]] // no NOTIFY
        int m_id = 0;
};

// ----------------------------------------------------------

struct Logger {
    int         total      = 0;
    int         name_calls = 0;
    std::string last_name;

    [[= moc::slot]] void onAge(int v) { total += v; }

    [[= moc::slot]] void onName(std::string const &s) {
        ++name_calls;
        last_name = s;
    }
};

// ----------------------------------------------------------
// Properties
// ----------------------------------------------------------

TEST(Property, GetReflectsSet) {
    Person p;
    moc::set<"name">(p, std::string{"Ada"});
    moc::set<"age">(p, 30);
    EXPECT_EQ(moc::get<"name">(p), "Ada");
    EXPECT_EQ(moc::get<"age">(p), 30);
}

TEST(Property, NotifyFiresOnChange) {
    Person p;
    Logger l;
    moc::connect<"ageChanged", "onAge">(p, l);
    moc::set<"age">(p, 30);
    EXPECT_EQ(l.total, 30);
}

TEST(Property, EqualityGatedSetIsSilent) {
    Person p;
    Logger l;
    moc::connect<"ageChanged", "onAge">(p, l);
    moc::set<"age">(p, 30);
    moc::set<"age">(p, 30); // same value -> no NOTIFY
    EXPECT_EQ(l.total, 30); // fired exactly once
}

TEST(Property, NameNotifyDeliversValue) {
    Person p;
    Logger l;
    moc::connect<"nameChanged", "onName">(p, l);
    moc::set<"name">(p, std::string{"Ada"});
    EXPECT_EQ(l.name_calls, 1);
    EXPECT_EQ(l.last_name, "Ada");
}

TEST(Property, NoNotifyDeclaredStillSets) {
    Person p;
    moc::set<"id">(p, 42); // no NOTIFY signal declared
    EXPECT_EQ(moc::get<"id">(p), 42);
}

// ----------------------------------------------------------
// Signal core
// ----------------------------------------------------------

TEST(Signal, EmitCallsEverySlot) {
    moc::Signal<int> sig;
    int              sum = 0;
    sig.connect([&](int v) { sum += v; });
    sig.connect([&](int v) { sum += v * 10; });
    sig(3);
    EXPECT_EQ(sum, 33);
}

TEST(Signal, DisconnectById) {
    moc::Signal<int> sig;
    int              a = 0, b = 0;
    auto             id = sig.connect([&](int v) { a += v; });
    sig.connect([&](int v) { b += v; });

    sig(1);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);

    EXPECT_TRUE(sig.disconnect(id));
    EXPECT_FALSE(sig.disconnect(id)); // already gone

    sig(1);
    EXPECT_EQ(a, 1); // disconnected slot did not fire
    EXPECT_EQ(b, 2);
}

TEST(Signal, DisconnectAll) {
    moc::Signal<int> sig;
    int              n = 0;
    sig.connect([&](int) { ++n; });
    sig.connect([&](int) { ++n; });
    sig.disconnect_all();
    sig(1);
    EXPECT_EQ(n, 0);
}

TEST(Signal, ReentrantSelfDisconnect) {
    moc::Signal<int>     sig;
    int                  hits = 0;
    moc::Signal<int>::Id self{};
    self = sig.connect([&](int) {
        ++hits;
        sig.disconnect(self); // disconnect itself mid-emission
    });
    sig.connect([&](int) { ++hits; });

    sig(1); // both fire
    sig(2); // only the second remains
    EXPECT_EQ(hits, 3);
}

// ----------------------------------------------------------
// ScopedConnection
// ----------------------------------------------------------

TEST(ScopedConnection, AutoDisconnectAtScopeExit) {
    moc::Signal<int> sig;
    int              hits = 0;
    {
        auto sc = sig.scoped_connect([&](int) { ++hits; });
        sig(1);
        sig(1);
    } // sc destroyed -> auto-disconnect
    sig(1);
    EXPECT_EQ(hits, 2);
}

TEST(ScopedConnection, ResetDisconnectsEarly) {
    moc::Signal<int> sig;
    int              hits = 0;
    auto             sc   = sig.scoped_connect([&](int) { ++hits; });
    sig(1);
    EXPECT_TRUE(sc.connected());
    sc.reset();
    EXPECT_FALSE(sc.connected());
    sig(1);
    EXPECT_EQ(hits, 1);
}

TEST(ScopedConnection, ReleaseKeepsConnectionAlive) {
    moc::Signal<int> sig;
    int              hits = 0;
    {
        auto sc = sig.scoped_connect([&](int) { ++hits; });
        sc.release(); // detach ownership; connection survives the wrapper
        EXPECT_FALSE(sc.connected());
    }
    sig(1);
    EXPECT_EQ(hits, 1);
}

TEST(ScopedConnection, MoveTransfersOwnership) {
    moc::Signal<int> sig;
    int              hits = 0;
    auto             sc   = sig.scoped_connect([&](int) { ++hits; });
    {
        auto moved = std::move(sc);
        EXPECT_TRUE(moved.connected());
        EXPECT_FALSE(sc.connected());
        sig(1);
        EXPECT_EQ(hits, 1);
    } // moved destroyed -> disconnect
    sig(1);
    EXPECT_EQ(hits, 1); // unchanged: connection gone
}
