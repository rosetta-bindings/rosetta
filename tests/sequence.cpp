// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for foreign-sequence marshalling (rosetta::is_sequence)
// in the *-expanded backends.
//
// A trait-registered container (GEO::vector-style) keeps kind "unknown" in
// the IR — backends that don't opt in skip it — and the opted-in backends
// (python/nanobind/node/wasm/lua-expanded + typescript) marshal it by COPY
// through a std::vector<element> boundary inside an emitted adapter. The
// adapter calls the method BY NAME with concrete arguments, so an overload
// set whose surviving IR entry is the sequence overload binds too (the
// geogram motivation: MeshVertices::assign_points, first-declared overload
// takes GEO::vector<double>&), and a mutable Seq& parameter binds
// input-only.
//
// Verifies the generated sources (render), not a live build — mirroring
// member_object.cpp.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>
#include <vector>

// A foreign sequence container: NOT std::vector (a distinct template), but
// vector-like — exactly the GEO::vector<T> shape.
namespace seqns {
    template <typename T> class svec : public std::vector<T> {
      public:
        using std::vector<T>::vector;
    };
} // namespace seqns

template <typename T> struct rosetta::is_sequence<seqns::svec<T>> : std::true_type {};

struct SeqGeom {
    seqns::svec<double> weights; // sequence field

    // Overload set whose FIRST declaration is the sequence one — the walk's
    // dedup keeps it, and the adapter resolves the call by name.
    void set_points(seqns::svec<double> &pts, int dim, bool steal) {
        (void)pts;
        (void)dim;
        (void)steal;
    }
    void set_points(const double *pts, int n) {
        (void)pts;
        (void)n;
    }

    seqns::svec<double> points() const { return {}; } // sequence return

    static seqns::svec<int> make(int n) { return seqns::svec<int>(std::size_t(n)); }

    int nb() const { return 0; } // plain method, member-pointer path untouched
};
template <> struct rosetta::binding_info<SeqGeom> {
    static constexpr const char *header = "seqgeom.h";
};

static std::string source_for(const char *lang) {
    const auto c = rosetta::gen_detail::make_context<SeqGeom>("seqtest");
    return rosetta::backend_registry().at(lang)->render(c);
}

// ---- IR spelling -------------------------------------------------------------
// The composed qualified spelling (namespace + template identifier + element)
// is what every adapter constructs.

TEST(Sequence, PythonAdapterSpellsQualifiedContainer) {
    const std::string s = source_for("python-expanded");
    EXPECT_NE(s.find("seqns::svec<double> seq0;"), std::string::npos);
    EXPECT_NE(s.find("std::copy(arg0.begin(), arg0.end(), seq0.begin());"), std::string::npos);
}

// ---- python-expanded ---------------------------------------------------------

TEST(Sequence, PythonEmitsAdapterForOverloadedSeqMethod) {
    const std::string s = source_for("python-expanded");
    // Adapter lambda with the std::vector boundary — bound even though
    // set_points is an overload set (the sequence overload survived).
    EXPECT_NE(s.find("c.def(\"set_points\", [](SeqGeom &self, std::vector<double> arg0"),
              std::string::npos);
    // No bare member pointer for the overload set.
    EXPECT_EQ(s.find("&SeqGeom::set_points"), std::string::npos);
    // Sequence return flattens to the boundary vector.
    EXPECT_NE(s.find("std::vector<double>(r.begin(), r.end())"), std::string::npos);
    // Sequence field rides a def_property copy.
    EXPECT_NE(s.find("c.def_property(\"weights\","), std::string::npos);
    // Plain method keeps the member-pointer path.
    EXPECT_NE(s.find("c.def(\"nb\", &SeqGeom::nb"), std::string::npos);
}

TEST(Sequence, PythonStaticSeqMethod) {
    const std::string s = source_for("python-expanded");
    EXPECT_NE(s.find("c.def_static(\"make\", []("), std::string::npos);
    EXPECT_NE(s.find("std::vector<int>(r.begin(), r.end())"), std::string::npos);
}

// ---- nanobind-expanded -------------------------------------------------------

TEST(Sequence, NanobindEmitsAdapter) {
    const std::string s = source_for("nanobind-expanded");
    EXPECT_NE(s.find(".def(\"set_points\", [](SeqGeom &self, std::vector<double> arg0"),
              std::string::npos);
    EXPECT_NE(s.find(".def_prop_rw(\"weights\","), std::string::npos);
    EXPECT_EQ(s.find("&SeqGeom::set_points"), std::string::npos);
}

// ---- node-expanded -----------------------------------------------------------

TEST(Sequence, NodeEmitsFreeAdapterAndBindsThroughExtMethod) {
    const std::string s = source_for("node-expanded");
    // Namespace-scope adapter (the runtime introspects ITS signature).
    EXPECT_NE(s.find("inline void seq_SeqGeom_set_points(SeqGeom &self, "
                     "std::vector<double> arg0"),
              std::string::npos);
    EXPECT_NE(s.find("ext_method<&rosetta_nx_seq::seq_SeqGeom_set_points>"),
              std::string::npos);
    // Static rides call_static on the adapter.
    EXPECT_NE(s.find("call_static<&rosetta_nx_seq::seq_SeqGeom_make>"), std::string::npos);
    // Sequence return flattens; sequence field stays out (no runtime accessor).
    EXPECT_NE(s.find("std::vector<double>(r.begin(), r.end())"), std::string::npos);
    EXPECT_EQ(s.find("get_field<&SeqGeom::weights>"), std::string::npos);
}

// ---- wasm-expanded -----------------------------------------------------------

TEST(Sequence, WasmEmitsAdapterAndRegistersBoundaryVector) {
    const std::string s = source_for("wasm-expanded");
    EXPECT_NE(s.find(".function(\"set_points\", +[](SeqGeom &self, std::vector<double> arg0"),
              std::string::npos);
    EXPECT_NE(s.find(".class_function(\"make\", +[]("), std::string::npos);
    // The std::vector boundary types are registered.
    EXPECT_NE(s.find("emscripten::register_vector<double>"), std::string::npos);
    EXPECT_NE(s.find("emscripten::register_vector<int>"), std::string::npos);
    // Sequence field rides a copying property.
    EXPECT_NE(s.find(".property(\"weights\","), std::string::npos);
    EXPECT_EQ(s.find("&SeqGeom::set_points"), std::string::npos);
}

// ---- lua-expanded ------------------------------------------------------------

TEST(Sequence, LuaEmitsUserdataAndTableOverloadPair) {
    const std::string s = source_for("lua-expanded");
    // Both forms dispatch: container userdata (what a bound sequence return
    // pushes) and a plain Lua table (sol::nested).
    EXPECT_NE(s.find("c[\"set_points\"] = sol::overload("), std::string::npos);
    EXPECT_NE(s.find("const std::vector<double> &arg0"), std::string::npos);
    EXPECT_NE(s.find("sol::nested<std::vector<double>> arg0"), std::string::npos);
    EXPECT_NE(s.find("seq0.resize(arg0.value().size());"), std::string::npos);
    EXPECT_NE(s.find("c[\"weights\"] = sol::property("), std::string::npos);
    EXPECT_EQ(s.find("&SeqGeom::set_points"), std::string::npos);
}

// ---- typescript --------------------------------------------------------------

TEST(Sequence, TypescriptDeclaresArrays) {
    const auto  c = rosetta::gen_detail::make_context<SeqGeom>("seqtest");
    // The .d.ts backend writes a file; check the type mapping directly.
    using rosetta::gen_detail::ts_type;
    ASSERT_FALSE(c.classes.empty());
    for (const auto &f : c.classes.front().fields) {
        if (f.name == "weights") {
            EXPECT_EQ(ts_type(f.type), "number[]");
        }
    }
    for (const auto &m : c.classes.front().methods) {
        if (m.name == "points") {
            EXPECT_EQ(ts_type(m.ret), "number[]");
        }
        if (m.name == "make") {
            EXPECT_EQ(ts_type(m.ret), "number[]");
        }
    }
}
