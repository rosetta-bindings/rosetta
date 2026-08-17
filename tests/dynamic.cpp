// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for the dynamic object model (<rosetta/dynamic.h>).
//
// The metadata below is HAND-BUILT, in exactly the shape the `dynamic` backend
// emits, so this file doubles as the emitter's specification: if
// backends/inline/dynamic.hxx produces something these tests would not accept, one of
// the two is wrong.
//
// Deliberately stock C++20 — no <experimental/meta>, no -freflection, no
// annotations. That is the claim the dynamic model has to keep: metadata is
// emitted as DATA, so the target compiles with an ordinary toolchain exactly
// like the -expanded backends' output.

#include <gtest/gtest.h>
#include <rosetta/dynamic.h>
#include <string>
#include <vector>

using namespace rosetta::dyn;

// ---------------------------------------------------------------------------
// The bound library — an ordinary header the generator never modifies.
// ---------------------------------------------------------------------------

namespace demo {

    enum class Colour { Red = 0, Green = 1, Blue = 2 };

    struct Person {
        std::string name;
        int         age = 0;
        std::string id     = "anon";
        Colour      colour = Colour::Red;

        Person() = default;
        Person(std::string n, int a) : name(std::move(n)), age(a) {}

        std::string greet(const std::string &salutation) const {
            return salutation + ", " + name + "!";
        }
        void birthday() { ++age; }

        // The overload set the name-keyed backends throw away today.
        double at(int i) const { return static_cast<double>(i); }
        double at(int i, int j) const { return static_cast<double>(i * j); }

        static Person make(const std::string &n) { return Person(n, 0); }

        std::vector<double> scores{1.0, 2.0, 3.0};
    };

} // namespace demo

// ---------------------------------------------------------------------------
// The generated metadata. This is what backends/inline/dynamic.hxx writes out.
// ---------------------------------------------------------------------------

namespace {

    // Forward declaration so a TypeDesc can name its own class: `&kPerson` is
    // an address constant expression even before the definition, which is what
    // lets two mutually-referencing classes link at static-init time with no
    // fix-up pass. (Registry::link() exists only for CROSS-module references.)
    extern const MetaClass kPerson;

    constexpr MetaEnumerator kColourValues[] = {
        {"Red", 0},
        {"Green", 1},
        {"Blue", 2},
    };

    // TypeDesc is the one mutable part of the emitted metadata — see
    // Registry::link().
    TypeDesc td_void{.kind = Kind::void_, .spelling = "void"};
    TypeDesc td_int{.kind = Kind::number, .spelling = "int", .integral = true};
    TypeDesc td_double{.kind = Kind::number, .spelling = "double", .integral = false};
    TypeDesc td_string{.kind = Kind::string, .spelling = "std::string"};
    TypeDesc td_colour{.kind          = Kind::enum_,
                       .spelling      = "demo::Colour",
                       .object        = "demo::Colour",
                       .enumerators   = kColourValues,
                       .n_enumerators = 3};
    TypeDesc td_vec_double{
        .kind = Kind::vector, .spelling = "std::vector<double>", .element = &td_double};
    TypeDesc td_person{.kind     = Kind::object,
                       .spelling = "demo::Person",
                       .object   = "demo::Person",
                       .cls      = &kPerson};

    // ---- fields ----

    const MetaField kPersonFields[] = {
        {.name = "name",
         .type = &td_string,
         .doc  = "Display name",
         .get  = +[](const ObjectRef &self, const ArgList &) {
             return make_any(&td_string, static_cast<demo::Person *>(self.ptr)->name);
         },
         .set = +[](const ObjectRef &self, const ArgList &a) {
             static_cast<demo::Person *>(self.ptr)->name = value_cast<std::string>(a[0]);
             return Any::none();
         }},
        {.name  = "age",
         .type  = &td_int,
         .doc   = "Age in years",
         .range = {.has = true, .lo = 0, .hi = 150},
         .get   = +[](const ObjectRef &self, const ArgList &) {
             return make_any(&td_int, static_cast<demo::Person *>(self.ptr)->age);
         },
         .set = +[](const ObjectRef &self, const ArgList &a) {
             static_cast<demo::Person *>(self.ptr)->age = value_cast<int>(a[0]);
             return Any::none();
         }},
        {.name     = "id",
         .type     = &td_string,
         .readonly = true,
         .get      = +[](const ObjectRef &self, const ArgList &) {
             return make_any(&td_string, static_cast<demo::Person *>(self.ptr)->id);
         }},
        {.name = "colour",
         .type = &td_colour,
         .get  = +[](const ObjectRef &self, const ArgList &) {
             return make_any(&td_colour, static_cast<demo::Person *>(self.ptr)->colour);
         },
         .set = +[](const ObjectRef &self, const ArgList &a) {
             static_cast<demo::Person *>(self.ptr)->colour = value_cast<demo::Colour>(a[0]);
             return Any::none();
         }},
        {.name = "scores",
         .type = &td_vec_double,
         .get  = +[](const ObjectRef &self, const ArgList &) {
             return make_any(&td_vec_double, static_cast<demo::Person *>(self.ptr)->scores);
         },
         .set = +[](const ObjectRef &self, const ArgList &a) {
             static_cast<demo::Person *>(self.ptr)->scores = value_cast<std::vector<double>>(a[0]);
             return Any::none();
         }},
    };

    // ---- methods ----

    const MetaParam kGreetParams[] = {{.name = "arg0", .type = &td_string, .is_ref = true}};
    const MetaParam kAt1Params[]   = {{.name = "arg0", .type = &td_int}};
    const MetaParam kAt2Params[]   = {{.name = "arg0", .type = &td_int},
                                      {.name = "arg1", .type = &td_int}};
    const MetaParam kMakeParams[]  = {{.name = "arg0", .type = &td_string, .is_ref = true}};

    const MetaMethod kPersonMethods[] = {
        {.name     = "greet",
         .ret      = &td_string,
         .doc      = "Greet with a salutation",
         .params   = kGreetParams,
         .n_params = 1,
         .is_const = true,
         .invoke   = +[](const ObjectRef &self, const ArgList &a) {
             return make_any(&td_string, static_cast<demo::Person *>(self.ptr)->greet(
                                             value_cast<std::string>(a[0])));
         }},
        {.name   = "birthday",
         .ret    = &td_void,
         .invoke = +[](const ObjectRef &self, const ArgList &) {
             static_cast<demo::Person *>(self.ptr)->birthday();
             return Any::none();
         }},
        // Both overloads reach the metadata — overload_index/count mirror
        // GenMethod's, and resolve() picks between them at call time.
        {.name           = "at",
         .ret            = &td_double,
         .params         = kAt1Params,
         .n_params       = 1,
         .is_const       = true,
         .overload_index = 0,
         .overload_count = 2,
         .invoke         = +[](const ObjectRef &self, const ArgList &a) {
             return make_any(&td_double,
                             static_cast<demo::Person *>(self.ptr)->at(value_cast<int>(a[0])));
         }},
        {.name           = "at",
         .ret            = &td_double,
         .params         = kAt2Params,
         .n_params       = 2,
         .is_const       = true,
         .overload_index = 1,
         .overload_count = 2,
         .invoke         = +[](const ObjectRef &self, const ArgList &a) {
             return make_any(&td_double, static_cast<demo::Person *>(self.ptr)->at(
                                             value_cast<int>(a[0]), value_cast<int>(a[1])));
         }},
        {.name      = "make",
         .ret       = &td_person,
         .params    = kMakeParams,
         .n_params  = 1,
         .is_static = true,
         .invoke    = +[](const ObjectRef &, const ArgList &a) {
             return make_any(&td_person, demo::Person::make(value_cast<std::string>(a[0])));
         }},
    };

    // ---- constructors ----

    const MetaParam kCtor2Params[] = {{.name = "arg0", .type = &td_string, .is_ref = false},
                                      {.name = "arg1", .type = &td_int}};

    const MetaCtor kPersonCtors[] = {
        {.construct = +[](const ArgList &) -> void * { return new demo::Person(); }},
        {.params    = kCtor2Params,
         .n_params  = 2,
         .construct = +[](const ArgList &a) -> void * {
             return new demo::Person(value_cast<std::string>(a[0]), value_cast<int>(a[1]));
         }},
    };

    const MetaClass kPerson{
        .name      = "Person",
        .qualified = "demo::Person",
        .doc       = "A person",
        .fields    = kPersonFields,
        .n_fields  = std::size(kPersonFields),
        .methods   = kPersonMethods,
        .n_methods = std::size(kPersonMethods),
        .ctors     = kPersonCtors,
        .n_ctors   = std::size(kPersonCtors),
        .destroy   = +[](void *p) { delete static_cast<demo::Person *>(p); },
        .self      = &td_person,
    };

    const MetaEnum kColour{.name       = "Colour",
                           .qualified  = "demo::Colour",
                           .underlying = "int",
                           .values     = kColourValues,
                           .n_values   = 3};

    // What the generated `register_<lib>()` does.
    struct Fixture : ::testing::Test {
        static void SetUpTestSuite() {
            registry().add_class(&kPerson);
            registry().add_enum(&kColour);
            registry().link();
        }
    };

} // namespace

// ---------------------------------------------------------------------------
// Introspection — what a UI walks to build its menus.
// ---------------------------------------------------------------------------

TEST_F(Fixture, RegistryFindsClassByBothNames) {
    EXPECT_EQ(registry().find_class("Person"), &kPerson);
    EXPECT_EQ(registry().find_class("demo::Person"), &kPerson);
    EXPECT_EQ(registry().find_class("Nope"), nullptr);
}

TEST_F(Fixture, FieldMetadataDrivesWidgetChoice) {
    const MetaField *age = kPerson.field("age");
    ASSERT_NE(age, nullptr);
    EXPECT_EQ(age->type->kind, Kind::number);
    EXPECT_TRUE(age->type->integral);
    EXPECT_TRUE(age->range.has);
    EXPECT_DOUBLE_EQ(age->range.hi, 150.0);
    EXPECT_STREQ(age->doc, "Age in years");

    // An enum field carries its enumerators, so a combobox needs no codegen.
    const MetaField *colour = kPerson.field("colour");
    ASSERT_NE(colour, nullptr);
    ASSERT_EQ(colour->type->n_enumerators, 3u);
    EXPECT_STREQ(colour->type->enumerators[2].name, "Blue");
}

TEST_F(Fixture, OverloadSetSurvivesIntoTheMetadata) {
    const auto at = kPerson.overloads("at");
    ASSERT_EQ(at.size(), 2u);
    EXPECT_EQ(at[0]->n_params, 1u);
    EXPECT_EQ(at[1]->n_params, 2u);
}

// ---------------------------------------------------------------------------
// Dynamic invocation
// ---------------------------------------------------------------------------

TEST_F(Fixture, ConstructReadWriteCall) {
    Result made = Object::create(kPerson, {Any::text("Ada"), Any::integer(36)});
    ASSERT_TRUE(made.ok()) << made.error;

    Object p = Object::adopt(kPerson, made.value.as_object().ptr, made.value.as_object().owner);

    Result name = p.get("name");
    ASSERT_TRUE(name.ok()) << name.error;
    EXPECT_EQ(name.value.as_string(), "Ada");

    Result greet = p.call("greet", {Any::text("Hello")});
    ASSERT_TRUE(greet.ok()) << greet.error;
    EXPECT_EQ(greet.value.as_string(), "Hello, Ada!");

    ASSERT_TRUE(p.call("birthday").ok());
    EXPECT_EQ(p.get("age").value.as_int(), 37);
}

TEST_F(Fixture, DefaultConstructorIsSelectedByArity) {
    Result made = Object::create(kPerson);
    ASSERT_TRUE(made.ok()) << made.error;
    Object p = Object::adopt(kPerson, made.value.as_object().ptr, made.value.as_object().owner);
    EXPECT_EQ(p.get("id").value.as_string(), "anon");
}

TEST_F(Fixture, OverloadResolutionPicksByArgumentCount) {
    demo::Person raw;
    Object       p = Object::borrow(kPerson, &raw);

    Result one = p.call("at", {Any::integer(7)});
    ASSERT_TRUE(one.ok()) << one.error;
    EXPECT_DOUBLE_EQ(one.value.as_number(), 7.0);

    Result two = p.call("at", {Any::integer(6), Any::integer(7)});
    ASSERT_TRUE(two.ok()) << two.error;
    EXPECT_DOUBLE_EQ(two.value.as_number(), 42.0);
}

TEST_F(Fixture, FailedResolutionNamesEveryCandidate) {
    demo::Person raw;
    Object       p = Object::borrow(kPerson, &raw);

    Result bad = p.call("at", {Any::text("nope")});
    EXPECT_FALSE(bad.ok());
    // The diagnostic the name-keyed backends cannot produce, having discarded
    // the siblings at generation time.
    EXPECT_NE(bad.error.find("matched no overload of 2"), std::string::npos) << bad.error;
    EXPECT_NE(bad.error.find("is not a int"), std::string::npos) << bad.error;
}

TEST_F(Fixture, StaticMethodNeedsNoReceiverAndReturnsAnOwnedObject) {
    Result r = call_static(kPerson, "make", {Any::text("Grace")});
    ASSERT_TRUE(r.ok()) << r.error;
    ASSERT_EQ(r.value.kind(), Kind::object);
    EXPECT_EQ(r.value.as_object().cls, &kPerson);
    EXPECT_TRUE(r.value.as_object().owner) << "a by-value return owns its copy";

    Object made = Object::adopt(kPerson, r.value.as_object().ptr, r.value.as_object().owner);
    EXPECT_EQ(made.get("name").value.as_string(), "Grace");
}

// ---------------------------------------------------------------------------
// Annotations enforced once, in the core
// ---------------------------------------------------------------------------

TEST_F(Fixture, ReadonlyIsRejected) {
    demo::Person raw;
    Object       p   = Object::borrow(kPerson, &raw);
    Result       bad = p.set("id", Any::text("root"));
    EXPECT_FALSE(bad.ok());
    EXPECT_NE(bad.error.find("read-only"), std::string::npos) << bad.error;
    EXPECT_EQ(raw.id, "anon");
}

TEST_F(Fixture, RangeIsEnforcedOnceForEveryBackend) {
    demo::Person raw;
    Object       p = Object::borrow(kPerson, &raw);

    ASSERT_TRUE(p.set("age", Any::integer(42)).ok());
    EXPECT_EQ(raw.age, 42);

    Result bad = p.set("age", Any::integer(999));
    EXPECT_FALSE(bad.ok());
    EXPECT_NE(bad.error.find("outside [0, 150]"), std::string::npos) << bad.error;
    EXPECT_EQ(raw.age, 42) << "a rejected write must not reach the object";
}

TEST_F(Fixture, WrongTypeIsRejectedBeforeTheThunkRuns) {
    demo::Person raw;
    Object       p   = Object::borrow(kPerson, &raw);
    Result       bad = p.set("age", Any::text("old"));
    EXPECT_FALSE(bad.ok());
    EXPECT_EQ(raw.age, 0);
}

// ---------------------------------------------------------------------------
// Marshalling
// ---------------------------------------------------------------------------

TEST_F(Fixture, VectorsRoundTripThroughTheCanonicalRepresentation) {
    demo::Person raw;
    Object       p = Object::borrow(kPerson, &raw);

    Result got = p.get("scores");
    ASSERT_TRUE(got.ok()) << got.error;
    ASSERT_EQ(got.value.kind(), Kind::vector);
    ASSERT_EQ(got.value.as_list().size(), 3u);
    EXPECT_DOUBLE_EQ(got.value.as_list()[1].as_number(), 2.0);

    ASSERT_TRUE(p.set("scores", Any::list({Any::real(9.5), Any::real(8.5)}, &td_vec_double)).ok());
    EXPECT_EQ(raw.scores, (std::vector<double>{9.5, 8.5}));
}

TEST_F(Fixture, AnUntypedListStillReadsAsAVectorAndReachesAVectorParameter) {
    // A language wrapper boxing a host-language sequence has no TypeDesc to
    // hand: there is no single C++ element type behind `[1, 2, 3]`. Any::list
    // must substitute a builtin the way integer() / real() / text() do —
    // kind() reads type_->kind, so a null descriptor used to report
    // Kind::unknown and match() rejected the list against EVERY vector
    // parameter.
    const Any untyped = Any::list({Any::real(9.5), Any::real(8.5)});

    EXPECT_EQ(untyped.kind(), Kind::vector);
    EXPECT_EQ(match(&td_vec_double, untyped), Match::exact);

    // ...and it still marshals, which is the behaviour the kind gates.
    demo::Person raw;
    Object       p = Object::borrow(kPerson, &raw);
    ASSERT_TRUE(p.set("scores", untyped).ok());
    EXPECT_EQ(raw.scores, (std::vector<double>{9.5, 8.5}));

    // An empty list fits any sequence; a mistyped one still loses, so the
    // fallback descriptor did not turn match() into a rubber stamp.
    EXPECT_EQ(match(&td_vec_double, Any::list({})), Match::exact);
    EXPECT_EQ(match(&td_vec_double, Any::list({Any::text("nope")})), Match::none);
}

TEST_F(Fixture, AnIntegerArgumentSatisfiesADoubleParameterByPromotion) {
    // The host language has one number type; requiring an exact match would
    // make every double-taking method uncallable from Lua or JavaScript.
    EXPECT_EQ(match(&td_double, Any::integer(3)), Match::promote);
    EXPECT_EQ(match(&td_double, Any::real(3.0)), Match::exact);
    EXPECT_EQ(match(&td_int, Any::text("3")), Match::none);
}

TEST_F(Fixture, EnumsCanonicaliseToIntegersButKeepTheirNames) {
    demo::Person raw;
    Object       p = Object::borrow(kPerson, &raw);

    ASSERT_TRUE(p.set("colour", Any::enumeration(2, &td_colour)).ok());
    EXPECT_EQ(raw.colour, demo::Colour::Blue);
    EXPECT_EQ(p.get("colour").value.to_string(), "demo::Colour::Blue");
}

TEST_F(Fixture, BorrowedHandlesDoNotOwn) {
    demo::Person raw;
    Object       p = Object::borrow(kPerson, &raw);
    EXPECT_FALSE(p.ref().owner) << "borrow() must not take ownership of a stack object";
}

TEST_F(Fixture, AnObjectRoundTripsToATypedAnySoItCanBeAnArgument) {
    // Without MetaClass::self the handle would come back kind-unknown and could
    // never be passed to another method — no `mesh.translate(vec)` at all.
    demo::Person raw;
    const Any    a = Object::borrow(kPerson, &raw).as_any();
    EXPECT_EQ(a.kind(), Kind::object);
    EXPECT_EQ(a.type(), &td_person);
    EXPECT_EQ(match(&td_person, a), Match::exact);
}
