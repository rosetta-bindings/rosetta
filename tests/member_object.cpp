// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

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
// Overload gating: the walk dedups overloads by name, but the emitters spell
// the bare `&T::name`, which is ambiguous for an overload set. The surviving
// IR entry is flagged is_overloaded and every runtime backend skips it.
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

// ---- python-expanded --------------------------------------------------------

TEST(MemberObject, PythonEmitsReferenceProperty) {
    const std::string s = source_for("python-expanded");
    EXPECT_NE(s.find("c.def_property_readonly(\"store\","), std::string::npos);
    EXPECT_NE(s.find("[](MoOwner &s) -> MoStore & { return s.store; }"), std::string::npos);
    EXPECT_NE(s.find("py::return_value_policy::reference_internal"), std::string::npos);
}

TEST(MemberObject, PythonStillBindsPlainFieldAndStoreMethods) {
    const std::string s = source_for("python-expanded");
    EXPECT_NE(s.find("c.def_readwrite(\"id\", &MoOwner::id"), std::string::npos);
    EXPECT_NE(s.find("c.def(\"nb\", &MoStore::nb"), std::string::npos);
    EXPECT_NE(s.find("c.def(\"grow\", &MoStore::grow"), std::string::npos);
}

TEST(MemberObject, PythonSkipsOverloadSetKeepsPlain) {
    const std::string s = source_for("python-expanded");
    EXPECT_EQ(s.find("&MoOver::f"), std::string::npos); // ambiguous — skipped
    EXPECT_NE(s.find("c.def(\"g\", &MoOver::g"), std::string::npos);
}

// ---- node-expanded ----------------------------------------------------------

TEST(MemberObject, NodeEmitsAliasedAccessor) {
    const std::string s = source_for("node-expanded");
    EXPECT_NE(s.find("get_member_object<&MoOwner::store>"), std::string::npos);
    EXPECT_NE(s.find("set_field_readonly<&MoOwner::store, \"store\">"), std::string::npos);
}

TEST(MemberObject, NodeSkipsOverloadSetKeepsPlain) {
    const std::string s = source_for("node-expanded");
    EXPECT_EQ(s.find("&MoOver::f"), std::string::npos);
    EXPECT_NE(s.find("call_method<&MoOver::g>"), std::string::npos);
}

// ---- wasm-expanded ----------------------------------------------------------

TEST(MemberObject, WasmEmitsBorrowedHandleGetter) {
    // embind properties copy, so the member object binds as a getter METHOD
    // returning a raw (non-owning) pointer: mesh.vertices().nb(). Unlike
    // pybind's reference_internal nothing pins the parent — documented in the
    // emitted comment.
    const std::string s = source_for("wasm-expanded");
    EXPECT_NE(s.find(".function(\"store\", +[](MoOwner &s) { return &s.store; }, "
                     "emscripten::allow_raw_pointers())"),
              std::string::npos);
}

// ---- lua-expanded -----------------------------------------------------------

TEST(MemberObject, LuaBindsMemberObjectReadonly) {
    // sol2 pushes a class member by REFERENCE, so the non-copyable member
    // object rides the ordinary readonly member-pointer path.
    const std::string s = source_for("lua-expanded");
    EXPECT_NE(s.find("sol::readonly(&MoOwner::store)"), std::string::npos);
}

// ---- "final" classes (manifest flag) -----------------------------------------

TEST(MemberObject, FinalSuppressesTrampolineAndUnlocksNodeAlias) {
    // Without final: trampolined store ⇒ no aliased accessor on node.
    {
        const auto c = rosetta::gen_detail::make_context<MoVirtOwner, MoVirtStore>("motest");
        const std::string s = rosetta::backend_registry().at("node-expanded")->render(c);
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
        const std::string s = rosetta::backend_registry().at("node-expanded")->render(c);
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
    const std::string s = rosetta::backend_registry().at("python-expanded")->render(c);
    EXPECT_EQ(s.find("Py_MoVirtStore"), std::string::npos);
    EXPECT_NE(s.find("c.def(\"nb\", &MoVirtStore::nb"), std::string::npos);
}

// ---- gates stay conservative where not implemented ---------------------------

TEST(MemberObject, WasmAndLuaSkipOverloadSet) {
    for (const char *lang : {"wasm-expanded", "lua-expanded"}) {
        const std::string s = source_for(lang);
        EXPECT_EQ(s.find("&MoOver::f"), std::string::npos) << lang;
    }
}
