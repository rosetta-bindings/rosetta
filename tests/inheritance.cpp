// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for inherited-member flattening in walk<T>().
//
// walk<T>() recurses public bases and emits their fields/methods alongside T's
// own, deduped by identifier. This exercises the three behaviours that matter:
//   - single inheritance: base fields/methods appear on the derived binding
//   - shadowing: a redeclared name collapses to ONE emission, derived wins
//   - diamond (virtual): a shared base is emitted exactly once
//   - a NON-PUBLIC redeclaration (protected override, private field) hides the
//     base's public one and neither is emitted
//
// Verification goes through rosetta::to_json / from_json (value-based, no
// codegen) for fields, and rosetta::to_markdown for inherited methods.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <rosetta/generate.h>            // rosetta::to_markdown
#include <rosetta/visitors/json_visitor.h> // rosetta::to_json / from_json
#include <string>

// ---- single inheritance ----------------------------------------------------
struct Shape {
    std::string name       = "";
    bool        visible    = true;
    double      draw_order = 0.0;

    std::string kind() const { return "shape"; }
};

struct Circle : Shape {
    double radius = 1.0;
};

// ---- shadowing: Derived redeclares a base field name -----------------------
struct Tagged {
    int         id  = 0;
    std::string tag = "base";
};

struct Retagged : Tagged {
    std::string tag = "derived"; // hides Tagged::tag
};

// ---- diamond (virtual base, so the shared subobject is unambiguous) --------
struct Top {
    int top_val = 1;
};
struct Left : virtual Top {
    int left_val = 2;
};
struct Right : virtual Top {
    int right_val = 3;
};
struct Bottom : Left, Right {
    int bottom_val = 4;
};

// ---- pure virtual in an abstract base, overridden in the derived -----------
struct Drawable {
    std::string label = "";

    virtual double surface_area() const = 0; // pure virtual
    virtual ~Drawable()                 = default;
};

struct Square : Drawable {
    double side = 1.0;

    double surface_area() const override { return side * side; } // overrides
};

// ---- non-public redeclaration of a public base name ------------------------
// A protected override hides the base's public declaration from every caller
// outside the class, the emitted binding included: `&Muted::speak` names an
// inaccessible member, so neither declaration can be bound. Same story for a
// private field shadowing a public base one.
struct Speaker {
    virtual std::string speak() const { return "base"; }
    std::string         volume() const { return "loud"; }
    virtual ~Speaker() = default;
};

class Muted : public Speaker {
  public:
    std::string name() const { return "muted"; }

  protected:
    std::string speak() const override { return "shh"; } // hides Speaker::speak
};

struct OpenBox {
    int value = 1;
    int spare = 2;
};

class ClosedBox : public OpenBox {
  public:
    int lid = 3;

  private:
    int value = 4; // hides OpenBox::value
};

// Minimal visitor that records, per method name, how many times walk<T>()
// emitted it and which class declared the emitted reflection. Lets us assert
// that an overridden virtual is emitted exactly once, as the derived override.
struct MethodCounter {
    std::map<std::string, int>         calls;
    std::map<std::string, std::string> owners;     // method name -> declaring class
    std::map<std::string, bool>        is_virtual;  // saw a virtual_spec annotation
    std::map<std::string, bool>        is_pure;     // virtual_spec.pure
    std::map<std::string, bool>        overrides;   // virtual_spec.overrides

    template <std::meta::info, auto...> void field(const char *) {}
    template <std::meta::info, auto...> void method_static(const char *) {}

    template <std::meta::info Fn, auto... Anns> void method_instance(const char *name) {
        calls[name]++;
        constexpr const char *owner =
            std::define_static_string(std::meta::identifier_of(std::meta::parent_of(Fn)));
        owners[name] = owner;

        constexpr bool                 v  = rosetta::ann::has<rosetta::virtual_spec>(Anns...);
        constexpr rosetta::virtual_spec vs =
            rosetta::ann::get_or<rosetta::virtual_spec>(rosetta::virtual_spec{}, Anns...);
        is_virtual[name] = v;
        is_pure[name]    = vs.pure;
        overrides[name]  = vs.overrides;
    }
};

// ---------------------------------------------------------------------------

TEST(Inheritance, BaseFieldsAppearOnDerived) {
    Circle c;
    c.name       = "c1";
    c.visible    = false;
    c.draw_order = 7.5;
    c.radius     = 2.0;

    const nlohmann::json j = rosetta::to_json(c);

    // inherited fields from Shape ...
    ASSERT_TRUE(j.contains("name"));
    ASSERT_TRUE(j.contains("visible"));
    ASSERT_TRUE(j.contains("draw_order"));
    // ... plus the derived field
    ASSERT_TRUE(j.contains("radius"));

    EXPECT_EQ(j["name"], "c1");
    EXPECT_EQ(j["visible"], false);
    EXPECT_EQ(j["draw_order"], 7.5);
    EXPECT_EQ(j["radius"], 2.0);
}

TEST(Inheritance, RoundTripPreservesInheritedFields) {
    Circle c;
    c.name       = "ring";
    c.draw_order = 3.0;
    c.radius     = 9.0;

    const Circle back = rosetta::from_json<Circle>(rosetta::to_json(c));

    EXPECT_EQ(back.name, "ring");
    EXPECT_EQ(back.draw_order, 3.0);
    EXPECT_EQ(back.radius, 9.0);
}

TEST(Inheritance, ShadowedFieldEmittedOnceDerivedWins) {
    Retagged r;
    r.id  = 42;
    r.tag = "derived"; // sets Retagged::tag

    const nlohmann::json j = rosetta::to_json(r);

    // Exactly two keys: the shadowed "tag" must not appear twice.
    EXPECT_EQ(j.size(), 2u);
    ASSERT_TRUE(j.contains("id"));
    ASSERT_TRUE(j.contains("tag"));
    EXPECT_EQ(j["id"], 42);
    EXPECT_EQ(j["tag"], "derived"); // most-derived declaration wins
}

TEST(Inheritance, DiamondSharedBaseEmittedOnce) {
    Bottom b;
    b.top_val = 11; // single shared Top subobject (virtual inheritance)

    const nlohmann::json j = rosetta::to_json(b);

    // top_val (once) + left_val + right_val + bottom_val == 4 keys.
    EXPECT_EQ(j.size(), 4u);
    ASSERT_TRUE(j.contains("top_val"));
    EXPECT_EQ(j["top_val"], 11);
    EXPECT_TRUE(j.contains("left_val"));
    EXPECT_TRUE(j.contains("right_val"));
    EXPECT_TRUE(j.contains("bottom_val"));
}

TEST(Inheritance, InheritedMethodIsDocumented) {
    // Circle inherits Shape::kind() — it should surface in the generated docs.
    const std::string md = rosetta::to_markdown<Circle>();
    EXPECT_NE(md.find("kind"), std::string::npos);

    std::cerr << "Generated Markdown for Circle:\n" << md << std::endl;
}

TEST(Inheritance, OverriddenVirtualEmittedOnceAsDerived) {
    MethodCounter counter;
    rosetta::walk<Square>(counter);

    // surface_area() is pure virtual in Drawable and overridden in Square.
    // Dedup must collapse the two declarations to a single emission ...
    ASSERT_EQ(counter.calls["surface_area"], 1);
    // ... and the surviving reflection must be Square's override, not the
    // base's pure-virtual declaration.
    EXPECT_EQ(counter.owners["surface_area"], "Square");
}

TEST(Inheritance, ProtectedOverrideHidesBaseMethod) {
    MethodCounter counter;
    rosetta::walk<Muted>(counter);

    // Neither declaration of speak() is reachable from outside Muted, so the
    // walk emits none — binding one would emit an `&Muted::speak` that does
    // not compile.
    EXPECT_EQ(counter.calls["speak"], 0);
    // Everything else is untouched: Muted's own method and the unrelated
    // inherited one still bind.
    EXPECT_EQ(counter.calls["name"], 1);
    ASSERT_EQ(counter.calls["volume"], 1);
    EXPECT_EQ(counter.owners["volume"], "Speaker");
}

TEST(Inheritance, PrivateFieldHidesBaseField) {
    ClosedBox b;
    b.lid = 9;

    const nlohmann::json j = rosetta::to_json(b);

    EXPECT_FALSE(j.contains("value")); // hidden by ClosedBox's private one
    ASSERT_TRUE(j.contains("spare"));  // sibling base field is unaffected
    ASSERT_TRUE(j.contains("lid"));
    EXPECT_EQ(j["spare"], 2);
    EXPECT_EQ(j["lid"], 9);
}

TEST(Inheritance, ConcreteOverrideSerializesInheritedField) {
    Square s;
    s.label = "sq";
    s.side  = 3.0;

    const nlohmann::json j = rosetta::to_json(s);
    EXPECT_EQ(j["label"], "sq"); // inherited from the abstract base
    EXPECT_EQ(j["side"], 3.0);
}

TEST(Inheritance, VirtualMethodCarriesVirtualSpec) {
    MethodCounter counter;
    rosetta::walk<Square>(counter);

    // The surviving surface_area() reflection is Square's override: virtual,
    // overriding a base virtual, but not itself pure.
    EXPECT_TRUE(counter.is_virtual["surface_area"]);
    EXPECT_TRUE(counter.overrides["surface_area"]);
    EXPECT_FALSE(counter.is_pure["surface_area"]);
}

TEST(Inheritance, NonVirtualMethodHasNoVirtualSpec) {
    MethodCounter counter;
    rosetta::walk<Circle>(counter);

    // Shape::kind() is a plain (non-virtual) inherited method — no spec.
    ASSERT_EQ(counter.calls["kind"], 1);
    EXPECT_FALSE(counter.is_virtual["kind"]);
}
