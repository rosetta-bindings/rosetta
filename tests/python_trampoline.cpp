// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for pybind11 trampoline generation.
//
// walk<T>() injects a rosetta::virtual_spec into a method's annotation pack for
// every virtual method; the IR (GenMethod) captures it (plus const/noexcept and
// exact signatures), and the Python backend turns that into a trampoline class
// so Python subclasses can override C++ virtuals.
//
// This verifies the *generated source* (PythonBackend::render) rather than a
// live pybind build — it asserts the trampoline class, the PYBIND11_OVERRIDE /
// _PURE shims, exact signatures, and the py::class_<T, Py_T> binding all appear,
// and that a class with no virtuals gets no trampoline.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

struct Animal {
    std::string name = "";

    virtual std::string speak() const  = 0;       // pure virtual  -> OVERRIDE_PURE
    virtual int         legs() const { return 4; } // virtual       -> OVERRIDE
    virtual void        feed(int grams) { (void)grams; }
    virtual ~Animal()                  = default;
};
template <> struct rosetta::binding_info<Animal> {
    static constexpr const char *header = "animal.h";
};

struct Plain { // no virtuals -> no trampoline
    int x = 0;
    int doubled() const { return x * 2; }
};
template <> struct rosetta::binding_info<Plain> {
    static constexpr const char *header = "plain.h";
};

static std::string python_source_for_animal() {
    const auto c = rosetta::gen_detail::make_context<Animal>("zoo");
    return rosetta::backend_registry().at("python")->render(c);
}

TEST(PyTrampoline, EmitsTrampolineClass) {
    const std::string s = python_source_for_animal();
    EXPECT_NE(s.find("namespace rosetta_py {"), std::string::npos);
    EXPECT_NE(s.find("class Py_Animal : public Animal {"), std::string::npos);
    EXPECT_NE(s.find("using Animal::Animal;"), std::string::npos);
}

TEST(PyTrampoline, PureVirtualUsesOverridePure) {
    const std::string s = python_source_for_animal();
    EXPECT_NE(s.find("PYBIND11_OVERRIDE_PURE(std::string, Animal, speak, )"), std::string::npos);
}

TEST(PyTrampoline, NonPureVirtualUsesOverride) {
    const std::string s = python_source_for_animal();
    EXPECT_NE(s.find("PYBIND11_OVERRIDE(int, Animal, legs, )"), std::string::npos);
}

TEST(PyTrampoline, ConstQualifierPreservedInSignature) {
    const std::string s = python_source_for_animal();
    EXPECT_NE(s.find("std::string speak() const override"), std::string::npos);
    EXPECT_NE(s.find("int legs() const override"), std::string::npos);
}

TEST(PyTrampoline, ParameterForwardedToMacro) {
    const std::string s = python_source_for_animal();
    // feed(int) is non-const virtual with one parameter; the trampoline names it
    // p0 and forwards it into the override macro.
    EXPECT_NE(s.find("void feed(int p0) override"), std::string::npos);
    EXPECT_NE(s.find("PYBIND11_OVERRIDE(void, Animal, feed, p0)"), std::string::npos);
}

TEST(PyTrampoline, BindsWithTrampolineType) {
    const std::string s = python_source_for_animal();
    EXPECT_NE(s.find("py::class_<Animal, rosetta_py::Py_Animal> c(m, \"Animal\")"),
              std::string::npos);
}

TEST(PyTrampoline, PlainClassHasNoTrampoline) {
    const auto        c = rosetta::gen_detail::make_context<Plain>("zoo");
    const std::string s = rosetta::backend_registry().at("python")->render(c);
    EXPECT_EQ(s.find("Py_Plain"), std::string::npos);
    EXPECT_EQ(s.find("namespace rosetta_py"), std::string::npos);
    EXPECT_NE(s.find("py::class_<Plain> c(m, \"Plain\")"), std::string::npos);
}
