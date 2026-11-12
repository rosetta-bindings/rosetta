// Copyright (c) fmaerten@gmail.com
// License: MIT

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

// A template instance: its exposed name ("TGrid<double, 3>") is no
// identifier, and its spelling carries a comma a macro argument would split.
template <class T, int N> struct TGrid {
    virtual ~TGrid() = default;
    virtual int size() const { return N; }
};
template <> struct rosetta::binding_info<TGrid<double, 3>> {
    static constexpr const char *header = "grid.h";
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

// ---- nanobind: the same virtual set, nanobind's macros -------------------------

namespace {
    std::string nanobind_source_for(const rosetta::GenContext &c) {
        return rosetta::backend_registry().at("nanobind")->render(c);
    }
} // namespace

TEST(NbTrampoline, EmitsTrampolineWithOneSlotPerOverride) {
    const std::string s = nanobind_source_for(rosetta::gen_detail::make_context<Animal>("zoo"));
    EXPECT_NE(s.find("#include <nanobind/trampoline.h>"), std::string::npos);
    EXPECT_NE(s.find("namespace rosetta_nb {"), std::string::npos);
    EXPECT_NE(s.find("using Nb_Animal_base = Animal;"), std::string::npos);
    EXPECT_NE(s.find("struct Nb_Animal : Nb_Animal_base {"), std::string::npos);
    EXPECT_NE(s.find("NB_TRAMPOLINE(Nb_Animal_base, 3);"), std::string::npos) << s;
}

TEST(NbTrampoline, OverridesUseNbOverrideAndKeepTheExactSignature) {
    const std::string s = nanobind_source_for(rosetta::gen_detail::make_context<Animal>("zoo"));
    EXPECT_NE(s.find("std::string speak() const override { NB_OVERRIDE_PURE(speak); }"),
              std::string::npos) << s;
    EXPECT_NE(s.find("int legs() const override { NB_OVERRIDE(legs); }"), std::string::npos);
    EXPECT_NE(s.find("void feed(int p0) override { NB_OVERRIDE(feed, p0); }"), std::string::npos);
}

TEST(NbTrampoline, AbstractClassBindsWithTheTrampolineAndGuardedConstructors) {
    const std::string s = nanobind_source_for(rosetta::gen_detail::make_context<Animal>("zoo"));
    EXPECT_NE(s.find("nb::class_<Animal, rosetta_nb::Nb_Animal>(m, \"Animal\""), std::string::npos)
        << s;
    // Animal is abstract: its constructor binds only if Nb_Animal is complete.
    EXPECT_NE(s.find("if constexpr (!std::is_abstract_v<rosetta_nb::Nb_Animal>)"),
              std::string::npos);
    EXPECT_NE(s.find("c.def(nb::init<>());"), std::string::npos);
}

TEST(NbTrampoline, PlainClassHasNoTrampoline) {
    const std::string s = nanobind_source_for(rosetta::gen_detail::make_context<Plain>("zoo"));
    EXPECT_EQ(s.find("Nb_Plain"), std::string::npos);
    EXPECT_EQ(s.find("namespace rosetta_nb"), std::string::npos);
    EXPECT_NE(s.find("nb::class_<Plain>(m, \"Plain\""), std::string::npos);
}

TEST(Trampolines, ATemplateInstanceGetsAnIdentifierAndAnAlias) {
    const auto c = rosetta::gen_detail::make_context<TGrid<double, 3>>("grid");
    const std::string nb = rosetta::backend_registry().at("nanobind")->render(c);
    EXPECT_NE(nb.find("using Nb_TGrid_double__3__base = TGrid<double, 3>;"), std::string::npos) << nb;
    EXPECT_NE(nb.find("NB_TRAMPOLINE(Nb_TGrid_double__3__base, 1);"), std::string::npos);
    const std::string py = rosetta::backend_registry().at("python")->render(c);
    EXPECT_NE(py.find("using Py_TGrid_double__3__base = TGrid<double, 3>;"), std::string::npos) << py;
    EXPECT_NE(py.find("using Py_TGrid_double__3__base::Py_TGrid_double__3__base;"), std::string::npos);
    EXPECT_NE(py.find("PYBIND11_OVERRIDE(int, Py_TGrid_double__3__base, size, )"), std::string::npos);
}
