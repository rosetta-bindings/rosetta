// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for N-API trampoline generation.
//
// Mirrors python_trampoline.cpp for the Node backend: verifies the generated
// auto_napi.cpp source (PythonBackend's sibling, NodeBackend::render). The N-API
// trampoline subclasses both T and rosetta::NapiTrampoline and routes each
// virtual through napi_call_override[_pure]. Runtime behaviour (JS subclass
// overriding a C++ virtual) is exercised by examples/trampoline-node.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

struct Shape {
    std::string label = "shape";

    virtual double      area() const = 0;                  // pure virtual
    virtual std::string describe() const { return label; } // virtual + default
    virtual void        scale(double k) { (void)k; }        // virtual, one param
    virtual ~Shape()                 = default;
};
template <> struct rosetta::binding_info<Shape> {
    static constexpr const char *header = "shapes.h";
};

struct Plain {
    int x = 0;
    int doubled() const { return x * 2; }
};
template <> struct rosetta::binding_info<Plain> {
    static constexpr const char *header = "plain.h";
};

static std::string node_source_for_shape() {
    const auto c = rosetta::gen_detail::make_context<Shape>("shapes");
    return rosetta::backend_registry().at("node")->render(c);
}

TEST(NodeTrampoline, EmitsTrampolineClass) {
    const std::string s = node_source_for_shape();
    EXPECT_NE(s.find("namespace rosetta_node {"), std::string::npos);
    EXPECT_NE(s.find("class Js_Shape : public Shape, public rosetta::NapiTrampoline {"),
              std::string::npos);
    EXPECT_NE(s.find("using Shape::Shape;"), std::string::npos);
}

TEST(NodeTrampoline, PureVirtualUsesOverridePure) {
    const std::string s = node_source_for_shape();
    EXPECT_NE(s.find("rosetta::napi_call_override_pure<Shape, double>(*this, \"area\")"),
              std::string::npos);
}

TEST(NodeTrampoline, NonPureVirtualFallsBackToBase) {
    const std::string s = node_source_for_shape();
    // describe() routes through napi_call_override with a base-call thunk.
    EXPECT_NE(s.find("napi_call_override<Shape, std::string>(*this, \"describe\""),
              std::string::npos);
    EXPECT_NE(s.find("this->Shape::describe();"), std::string::npos);
}

TEST(NodeTrampoline, ParameterForwardedToOverride) {
    const std::string s = node_source_for_shape();
    EXPECT_NE(s.find("void scale(double p0) override"), std::string::npos);
    EXPECT_NE(s.find("napi_call_override<Shape, void>(*this, \"scale\""), std::string::npos);
    EXPECT_NE(s.find("this->Shape::scale(p0);"), std::string::npos);
}

TEST(NodeTrampoline, BindsWithTrampolineType) {
    const std::string s = node_source_for_shape();
    // The wrapper HOLDS the trampoline, so a JS subclass's overrides are what
    // C++ virtual dispatch reaches.
    EXPECT_NE(s.find("using This = rosetta::Wrap<Shape, rosetta_node::Js_Shape>;"),
              std::string::npos);
    EXPECT_NE(s.find("exports.Set(\"Shape\", ctor);"), std::string::npos);
}

TEST(NodeTrampoline, PlainClassHasNoTrampoline) {
    const auto        c = rosetta::gen_detail::make_context<Plain>("shapes");
    const std::string s = rosetta::backend_registry().at("node")->render(c);
    EXPECT_EQ(s.find("Js_Plain"), std::string::npos);
    EXPECT_EQ(s.find("namespace rosetta_node"), std::string::npos);
    // No virtuals ⇒ the wrapper holds the class itself, not a Js_ trampoline.
    EXPECT_NE(s.find("using This = rosetta::Wrap<Plain, Plain>;"), std::string::npos);
    EXPECT_NE(s.find("exports.Set(\"Plain\", ctor);"), std::string::npos);
}
