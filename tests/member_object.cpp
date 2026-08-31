// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for member-object property binding and overload-set
// gating in the *-expanded backends.
//
// Member-object properties: a public data member whose type is another BOUND
// class but is non-copyable (it holds a back-reference to its owner, like
// GEO::Mesh::vertices) used to be skipped by the copyability gates. It is now
// emitted as a read-only property returning a REFERENCE to the member, with
// the parent kept alive (pybind reference_internal / the node runtime's
// aliased Wrap pinning the parent object).
//
// Overload gating: the walk emits EVERY overload, and each backend applies the
// policy its target language can honour. pybind11 / nanobind / jlcxx dispatch on
// argument types, so every entry binds; embind, N-API, sol2, the C#/Java op
// tables and REST routes key a method by name, so only the first-declared entry
// binds and the rest are recorded in the coverage report. Either way the member
// pointer is spelled through an explicit static_cast — the bare `&T::name` is
// ambiguous for a set and would not compile.
//
// Verifies the generated sources (render), not a live build — mirroring
// python_trampoline.cpp / node_trampoline.cpp.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

// Model of GEO::Mesh: the geometry lives in a non-copyable,
// non-default-constructible public member object holding a back-reference.
struct MoOwner;
struct MoStore {
    MoOwner &owner;
    int      count = 0;

    explicit MoStore(MoOwner &o) : owner(o) {}
    MoStore(const MoStore &)            = delete;
    MoStore &operator=(const MoStore &) = delete;

    int  nb() const { return count; }
    void grow(int n) { count += n; }
};
struct MoOwner {
    MoStore store;
    int     id = 7;

    MoOwner() : store(*this) {}
};
template <> struct rosetta::binding_info<MoOwner> {
    static constexpr const char *header = "mo.h";
};
template <> struct rosetta::binding_info<MoStore> {
    static constexpr const char *header = "mo.h";
};

// A store with PUBLIC VIRTUALS (the GEO::MeshVertices shape): normally a
// trampoline (Js_*) is generated, which disqualifies the class from node's
// aliased member-object path (the alias stores a T*, not a Js_T*). The
// manifest "final": true flag suppresses the trampoline — virtuals bind as
// plain callable methods and the alias becomes legal again.
struct MoVirtOwner;
struct MoVirtStore {
    MoVirtOwner &owner;
    explicit MoVirtStore(MoVirtOwner &o) : owner(o) {}
    MoVirtStore(const MoVirtStore &)            = delete;
    MoVirtStore &operator=(const MoVirtStore &) = delete;

    virtual ~MoVirtStore()   = default;
    virtual int  nb() const { return 5; }
    virtual void pop() {}
};
struct MoVirtOwner {
    MoVirtStore store;
    MoVirtOwner() : store(*this) {}
};
template <> struct rosetta::binding_info<MoVirtOwner> {
    static constexpr const char *header = "mo.h";
};
template <> struct rosetta::binding_info<MoVirtStore> {
    static constexpr const char *header = "mo.h";
};

// An overload set (f) next to a plain method (g).
struct MoOver {
    int f() const { return 1; }
    int f(int x) const { return x; }
    int g() const { return 2; }
};
template <> struct rosetta::binding_info<MoOver> {
    static constexpr const char *header = "mo.h";
};

static std::string source_for(const char *lang) {
    const auto c = rosetta::gen_detail::make_context<MoOwner, MoStore, MoOver>("motest");
    return rosetta::backend_registry().at(lang)->render(c);
}

// ---- python --------------------------------------------------------

TEST(MemberObject, PythonEmitsReferenceProperty) {
    const std::string s = source_for("python");
    EXPECT_NE(s.find("c.def_property_readonly(\"store\","), std::string::npos);
    EXPECT_NE(s.find("[](MoOwner &s) -> MoStore & { return s.store; }"), std::string::npos);
    EXPECT_NE(s.find("py::return_value_policy::reference_internal"), std::string::npos);
}

TEST(MemberObject, PythonStillBindsPlainFieldAndStoreMethods) {
    const std::string s = source_for("python");
    EXPECT_NE(s.find("c.def_readwrite(\"id\", &MoOwner::id"), std::string::npos);
    EXPECT_NE(s.find("c.def(\"nb\", &MoStore::nb"), std::string::npos);
    EXPECT_NE(s.find("c.def(\"grow\", &MoStore::grow"), std::string::npos);
}

TEST(MemberObject, PythonBindsEveryOverloadViaCastKeepsPlain) {
    const std::string s = source_for("python");
    // BOTH overloads bind: pybind11 collects repeated .def("f", …) into one
    // overload set and dispatches on the argument types. Each is spelled
    // through an explicit static_cast — the bare &MoOver::f is ambiguous.
    EXPECT_NE(s.find("c.def(\"f\", static_cast<int (MoOver::*)() const>(&MoOver::f)"),
              std::string::npos);
    EXPECT_NE(s.find("c.def(\"f\", static_cast<int (MoOver::*)(int) const>(&MoOver::f)"),
              std::string::npos);
    EXPECT_NE(s.find("c.def(\"g\", &MoOver::g"), std::string::npos);
}

TEST(MemberObject, NanobindBindsEveryOverload) {
    // nanobind used to skip an overload set outright (it spelled the bare
    // member pointer and could not have compiled one).
    const std::string s = source_for("nanobind");
    EXPECT_NE(s.find(".def(\"f\", static_cast<int (MoOver::*)() const>(&MoOver::f)"),
              std::string::npos);
    EXPECT_NE(s.find(".def(\"f\", static_cast<int (MoOver::*)(int) const>(&MoOver::f)"),
              std::string::npos);
}

TEST(MemberObject, JuliaBindsEveryOverload) {
    // jlcxx registers each as a Julia method of the same name; Julia's own
    // multiple dispatch picks by argument type.
    const std::string s = source_for("julia");
    EXPECT_NE(s.find("c.method(\"f\", static_cast<int (MoOver::*)() const>(&MoOver::f)"),
              std::string::npos);
    EXPECT_NE(s.find("c.method(\"f\", static_cast<int (MoOver::*)(int) const>(&MoOver::f)"),
              std::string::npos);
}

// ---- node ----------------------------------------------------------

TEST(MemberObject, NodeEmitsAliasedAccessor) {
    const std::string s = source_for("node");
    EXPECT_NE(s.find("get_member_object<&MoOwner::store>"), std::string::npos);
    EXPECT_NE(s.find("set_field_readonly<&MoOwner::store, \"store\">"), std::string::npos);
}

TEST(MemberObject, NodeBindsOverloadSurvivorViaAdapterKeepsPlain) {
    const std::string s = source_for("node");
    // No bare member pointer for the set — the survivor binds through a free
    // adapter that calls by name (overload resolution happens in the adapter).
    EXPECT_EQ(s.find("&MoOver::f"), std::string::npos);
    EXPECT_NE(s.find("ovl_MoOver_f"), std::string::npos);
    EXPECT_NE(s.find("ext_method<&rosetta_nx_seq::ovl_MoOver_f>"), std::string::npos);
    EXPECT_NE(s.find("call_method<&MoOver::g>"), std::string::npos);
}

// ---- wasm ----------------------------------------------------------

TEST(MemberObject, WasmEmitsBorrowedHandleGetter) {
    // embind properties copy, so the member object binds as a getter METHOD
    // returning a raw (non-owning) pointer: mesh.vertices().nb(). Unlike
    // pybind's reference_internal nothing pins the parent — documented in the
    // emitted comment.
    const std::string s = source_for("wasm");
    EXPECT_NE(s.find(".function(\"store\", +[](MoOwner &s) { return &s.store; }, "
                     "emscripten::allow_raw_pointers())"),
              std::string::npos);
}

// ---- lua -----------------------------------------------------------

TEST(MemberObject, LuaBindsMemberObjectReadonly) {
    // sol2 pushes a class member by REFERENCE, so the non-copyable member
    // object rides the ordinary readonly member-pointer path.
    const std::string s = source_for("lua");
    EXPECT_NE(s.find("sol::readonly(&MoOwner::store)"), std::string::npos);
}

// ---- "final" classes (manifest flag) -----------------------------------------

TEST(MemberObject, FinalSuppressesTrampolineAndUnlocksNodeAlias) {
    // Without final: trampolined store ⇒ no aliased accessor on node.
    {
        const auto c = rosetta::gen_detail::make_context<MoVirtOwner, MoVirtStore>("motest");
        const std::string s = rosetta::backend_registry().at("node")->render(c);
        EXPECT_NE(s.find("Js_MoVirtStore"), std::string::npos);
        EXPECT_EQ(s.find("get_member_object<&MoVirtOwner::store>"), std::string::npos);
    }
    // With final (what manifest "final": true sets): no trampoline, alias on.
    {
        auto c = rosetta::gen_detail::make_context<MoVirtOwner, MoVirtStore>("motest");
        for (auto &k : c.classes) {
            if (k.name == "MoVirtStore") {
                k.is_final = true;
            }
        }
        const std::string s = rosetta::backend_registry().at("node")->render(c);
        EXPECT_EQ(s.find("Js_MoVirtStore"), std::string::npos);
        EXPECT_NE(s.find("get_member_object<&MoVirtOwner::store>"), std::string::npos);
        // The virtuals still bind as plain callable methods.
        EXPECT_NE(s.find("call_method<&MoVirtStore::nb>"), std::string::npos);
    }
}

TEST(MemberObject, FinalSuppressesPythonTrampoline) {
    auto c = rosetta::gen_detail::make_context<MoVirtOwner, MoVirtStore>("motest");
    for (auto &k : c.classes) {
        if (k.name == "MoVirtStore") {
            k.is_final = true;
        }
    }
    const std::string s = rosetta::backend_registry().at("python")->render(c);
    EXPECT_EQ(s.find("Py_MoVirtStore"), std::string::npos);
    EXPECT_NE(s.find("c.def(\"nb\", &MoVirtStore::nb"), std::string::npos);
}

// ---- gates stay conservative where not implemented ---------------------------

TEST(MemberObject, NameKeyedBackendsBindExactlyTheFirstOverload) {
    // embind and sol2 both key a method by name — a second registration is a
    // duplicate (embind throws at module init; sol2's assignment overwrites),
    // so exactly ONE entry of the set binds, and it is the first-declared one.
    const std::string w = source_for("wasm");
    EXPECT_NE(w.find(".function(\"f\", static_cast<int (MoOver::*)() const>(&MoOver::f)"),
              std::string::npos);
    EXPECT_EQ(w.find("static_cast<int (MoOver::*)(int) const>(&MoOver::f)"),
              std::string::npos);

    // lua used to skip the whole set; it now binds the first overload.
    const std::string l = source_for("lua");
    EXPECT_NE(l.find("c[\"f\"] = static_cast<int (MoOver::*)() const>(&MoOver::f)"),
              std::string::npos);
    EXPECT_EQ(l.find("static_cast<int (MoOver::*)(int) const>(&MoOver::f)"),
              std::string::npos);
}

TEST(MemberObject, CsharpQualifiesTheOverloadedMemberPointer) {
    // The op table is keyed by name, so one entry binds — but it must be
    // spelled with the cast: `&MoOver::f` names the whole set and would not
    // compile. (This was already true before the walk emitted every overload.)
    const std::string s = source_for("csharp");
    EXPECT_NE(s.find("call_method<static_cast<int (MoOver::*)() const>(&MoOver::f)>"),
              std::string::npos);
    EXPECT_EQ(s.find("call_method<&MoOver::f>"), std::string::npos);
}
