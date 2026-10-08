// Copyright (c) fmaerten@gmail.com
// License: MIT

// Shared by tests/mcp.cpp and tests/mcp_scripting.cpp: a small bound library
// (demo::Person) and its metadata, hand-built in the shape the `dynamic` /
// `mcp` backends emit (see tests/dynamic.cpp for the annotated version), plus
// a fixture that drives an MCP server over it. Stock C++20.
//
// Header-only and included by exactly one TU per test executable.

#pragma once

#include <gtest/gtest.h>
#include <rosetta/runtime/mcp.h>
#include <sstream>
#include <string>
#include <vector>

using namespace rosetta::dyn;
using rosetta::mcp::json;

namespace demo {

    enum class Colour { Red = 0, Green = 1, Blue = 2 };

    struct Person {
        std::string         name;
        int                 age    = 0;
        std::string         id     = "anon";
        Colour              colour = Colour::Red;
        std::vector<double> scores{1.0, 2.0, 3.0};

        Person() = default;
        Person(std::string n, int a) : name(std::move(n)), age(a) {}

        double at(int i) const { return static_cast<double>(i); }
        double at(int i, int j) const { return static_cast<double>(i * j); }
        bool   olderThan(const Person &o) const { return age > o.age; }

        static Person make(const std::string &n) { return Person(n, 0); }
    };

} // namespace demo

namespace {

    extern const MetaClass kPerson;

    constexpr MetaEnumerator kColourValues[] = {{"Red", 0}, {"Green", 1}, {"Blue", 2}};

    TypeDesc td_int{.kind = Kind::number, .spelling = "int", .integral = true};
    TypeDesc td_double{.kind = Kind::number, .spelling = "double", .integral = false};
    TypeDesc td_bool{.kind = Kind::boolean, .spelling = "bool"};
    TypeDesc td_string{.kind = Kind::string, .spelling = "std::string"};
    TypeDesc td_colour{.kind          = Kind::enum_,
                       .spelling      = "demo::Colour",
                       .object        = "demo::Colour",
                       .enumerators   = kColourValues,
                       .n_enumerators = 3};
    TypeDesc td_scores{
        .kind = Kind::vector, .spelling = "std::vector<double>", .element = &td_double};
    TypeDesc td_person{
        .kind = Kind::object, .spelling = "demo::Person", .object = "demo::Person", .cls = &kPerson};

    demo::Person &self_of(const ObjectRef &r) { return *static_cast<demo::Person *>(r.ptr); }

    const MetaField kFields[] = {
        {.name = "name",
         .type = &td_string,
         .get  = +[](const ObjectRef &s, const ArgList &) { return make_any(&td_string, self_of(s).name); },
         .set  = +[](const ObjectRef &s, const ArgList &a) {
             self_of(s).name = value_cast<std::string>(a[0]);
             return Any::none();
         }},
        {.name  = "age",
         .type  = &td_int,
         .doc   = "Age in years",
         .range = {.has = true, .lo = 0, .hi = 150},
         .get   = +[](const ObjectRef &s, const ArgList &) { return make_any(&td_int, self_of(s).age); },
         .set   = +[](const ObjectRef &s, const ArgList &a) {
             self_of(s).age = value_cast<int>(a[0]);
             return Any::none();
         }},
        {.name     = "id",
         .type     = &td_string,
         .readonly = true,
         .get      = +[](const ObjectRef &s, const ArgList &) { return make_any(&td_string, self_of(s).id); }},
        {.name = "colour",
         .type = &td_colour,
         .get  = +[](const ObjectRef &s, const ArgList &) { return make_any(&td_colour, self_of(s).colour); },
         .set  = +[](const ObjectRef &s, const ArgList &a) {
             self_of(s).colour = value_cast<demo::Colour>(a[0]);
             return Any::none();
         }},
        {.name = "scores",
         .type = &td_scores,
         .get  = +[](const ObjectRef &s, const ArgList &) { return make_any(&td_scores, self_of(s).scores); },
         .set  = +[](const ObjectRef &s, const ArgList &a) {
             self_of(s).scores = value_cast<std::vector<double>>(a[0]);
             return Any::none();
         }},
    };

    const MetaParam kAt1[]    = {{.name = "i", .type = &td_int}};
    const MetaParam kAt2[]    = {{.name = "i", .type = &td_int}, {.name = "j", .type = &td_int}};
    const MetaParam kOlder[]  = {{.name = "other", .type = &td_person, .is_ref = true}};
    const MetaParam kMake[]   = {{.name = "n", .type = &td_string, .is_ref = true}};

    const MetaMethod kMethods[] = {
        {.name = "at", .ret = &td_double, .params = kAt1, .n_params = 1, .is_const = true,
         .overload_index = 0, .overload_count = 2,
         .invoke = +[](const ObjectRef &s, const ArgList &a) {
             return make_any(&td_double, self_of(s).at(value_cast<int>(a[0])));
         }},
        {.name = "at", .ret = &td_double, .params = kAt2, .n_params = 2, .is_const = true,
         .overload_index = 1, .overload_count = 2,
         .invoke = +[](const ObjectRef &s, const ArgList &a) {
             return make_any(&td_double,
                             self_of(s).at(value_cast<int>(a[0]), value_cast<int>(a[1])));
         }},
        {.name = "olderThan", .ret = &td_bool, .params = kOlder, .n_params = 1, .is_const = true,
         .invoke = +[](const ObjectRef &s, const ArgList &a) {
             return make_any(&td_bool, self_of(s).olderThan(ref_cast<const demo::Person>(a[0])));
         }},
        {.name = "make", .ret = &td_person, .params = kMake, .n_params = 1, .is_static = true,
         .invoke = +[](const ObjectRef &, const ArgList &a) {
             return make_any(&td_person, demo::Person::make(value_cast<std::string>(a[0])));
         }},
    };

    const MetaParam kCtor2[] = {{.name = "name", .type = &td_string}, {.name = "age", .type = &td_int}};
    const MetaCtor  kCtors[] = {
        {.construct = +[](const ArgList &) -> void * { return new demo::Person(); }},
        {.params = kCtor2, .n_params = 2, .construct = +[](const ArgList &a) -> void * {
             return new demo::Person(value_cast<std::string>(a[0]), value_cast<int>(a[1]));
         }},
    };

    const MetaClass kPerson{
        .name      = "Person",
        .qualified = "demo::Person",
        .doc       = "A person",
        .fields    = kFields,
        .n_fields  = std::size(kFields),
        .methods   = kMethods,
        .n_methods = std::size(kMethods),
        .ctors     = kCtors,
        .n_ctors   = std::size(kCtors),
        .destroy   = +[](void *p) { delete static_cast<demo::Person *>(p); },
        .self      = &td_person,
    };

    const MetaEnum kColour{.name = "Colour", .qualified = "demo::Colour", .values = kColourValues,
                           .n_values = 3};

    struct Mcp : ::testing::Test {
        static void SetUpTestSuite() {
            registry().add_class(&kPerson);
            registry().add_enum(&kColour);
            registry().link();
        }

        rosetta::mcp::Server server{rosetta::mcp::Options{.name = "demo", .max_list_items = 2}};
        int                  next_id = 1;

        json rpc(const std::string &method, json params = json::object()) {
            auto r = server.handle(
                {{"jsonrpc", "2.0"}, {"id", next_id++}, {"method", method}, {"params", params}});
            EXPECT_TRUE(r.has_value());
            return r ? *r : json();
        }

        // Call a tool; returns {isError, structured-or-text}.
        std::pair<bool, json> tool(const std::string &name, json args = json::object()) {
            json r = rpc("tools/call", {{"name", name}, {"arguments", args}});
            const json &res = r["result"];
            if (res["isError"].get<bool>()) {
                return {true, res["content"][0]["text"]};
            }
            return {false, res["structuredContent"]};
        }
    };

} // namespace
