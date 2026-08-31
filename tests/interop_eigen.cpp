// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for foreign-library interop (manifest "interop": ["eigen"],
// rosetta/interop.h).
//
// A method taking or returning an Eigen type used to be described as an
// ordinary class (kind "object"), which every backend bound and which then
// threw at CALL time because no caster for it was ever registered. Opting in
// marks those types instead: kind stays "unknown", so a backend with no caster
// skips the member, while python- / nanobind emit their caster header
// and bind the exact spelling unchanged.
//
// Recognition is by ENCLOSING NAMESPACE, which is what lets this suite run
// with no Eigen dependency at all: the stub below is `Eigen`-owned, and that
// is the entire criterion.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>

// Stand-in for a dense Eigen type: a class template in namespace Eigen with an
// alias for the concrete spelling, matching how VectorXd is really declared
// (Eigen::Matrix<double, -1, 1, …>) — so the test exercises the same
// template-identifier re-qualification a real signature needs.
namespace Eigen {
    template <typename T, int Rows, int Cols> struct Matrix {
        T data[Rows < 0 ? 1 : Rows];
    };
    using VectorXd = Matrix<double, -1, 1>;
} // namespace Eigen

namespace itp {
    class Solver {
    public:
        Solver() = default;

        // The shape that forced the hand-written extension files.
        Eigen::VectorXd solution() const { return {}; }
        void            setRhs(const Eigen::VectorXd &b) { rhs_ = b; }

        // An OVERLOAD naming the foreign type: the surviving (first-declared)
        // entry binds through an explicit static_cast to its exact signature,
        // which is the one path where the spelling has to be written out.
        void scale(const Eigen::VectorXd &v) { rhs_ = v; }
        void scale(double) {}

        // Untouched by the interop: must keep binding exactly as before.
        double residual() const { return 0.0; }

    private:
        Eigen::VectorXd rhs_;
    };
} // namespace itp

template <> struct rosetta::binding_info<itp::Solver> {
    static constexpr const char *header = "Solver.h";
};

// The opt-in the manifest's "interop": ["eigen"] emits into bindings.h.
template <> struct rosetta::interop_enabled<rosetta::eigen_interop> : std::true_type {};

namespace {

    const rosetta::GenMethod &method_named(const rosetta::GenClass &k, const std::string &n) {
        for (const auto &m : k.methods) {
            if (m.name == n) {
                return m;
            }
        }
        throw std::runtime_error("no method " + n);
    }

} // namespace

// The IR marks the foreign type and leaves `kind` "unknown", so a backend that
// never heard of the library keeps skipping it — while still recording the
// qualified identifier the opted-in backends need to spell it.
TEST(InteropEigen, IrMarksTheForeignTypeAndLeavesKindUnknown) {
    const auto c = rosetta::gen_detail::make_context<itp::Solver>("itest");
    ASSERT_EQ(c.classes.size(), 1u);
    const auto &k = c.classes.front();

    const auto &sol = method_named(k, "solution");
    EXPECT_EQ(sol.ret.interop, "eigen");
    EXPECT_EQ(sol.ret.kind, "unknown") << "kind must stay unknown, like is_pointer/is_sequence";
    // The qualified spelling keeps the namespace AND the template arguments —
    // what an emitted signature has to name, since the `using namespace` block
    // only opens the bound namespaces.
    EXPECT_EQ(sol.ret.object_qualified, "Eigen::Matrix<double, -1, 1>");

    const auto &rhs = method_named(k, "setRhs");
    ASSERT_EQ(rhs.params.size(), 1u);
    EXPECT_EQ(rhs.params.front().type.interop, "eigen");

    // An ordinary member is untouched.
    EXPECT_EQ(method_named(k, "residual").ret.kind, "number");
    EXPECT_TRUE(method_named(k, "residual").ret.interop.empty());

    // The enabled set reaches the backends through the context.
    ASSERT_EQ(c.interop.size(), 1u);
    EXPECT_EQ(c.interop.front(), "eigen");
}

// python and nanobind emit their caster header and bind the
// members; the type is spelled with its namespace, since the emitted
// `using namespace` block only opens the BOUND namespaces.
TEST(InteropEigen, PythonFamilyBindsThroughTheCaster) {
    const auto c = rosetta::gen_detail::make_context<itp::Solver>("itest");

    struct Case {
        const char *lang;
        const char *header;
    };
    for (const Case cs : {Case{"python", "pybind11/eigen.h"},
                          Case{"nanobind", "nanobind/eigen/dense.h"}}) {
        const std::string out = rosetta::backend_registry().at(cs.lang)->render(c);
        EXPECT_NE(out.find(std::string("#include <") + cs.header + ">"), std::string::npos)
            << cs.lang << " did not emit the caster header";
        EXPECT_NE(out.find("\"solution\""), std::string::npos)
            << cs.lang << " skipped the Eigen-returning method";
        EXPECT_NE(out.find("\"setRhs\""), std::string::npos)
            << cs.lang << " skipped the Eigen-taking method";
    }
}

// The one path that spells the foreign type out instead of leaving it to
// template deduction: an overload set binds through an explicit static_cast to
// the surviving entry's exact signature. That spelling must be namespace-
// qualified — display_string_of prints the template name bare
// ("Matrix<double, -1, 1>") and the emitted `using namespace` block opens only
// the BOUND namespaces, so the bare token would not resolve.
// (python only: nanobind skips overload sets outright.)
TEST(InteropEigen, OverloadCastSpellsTheTypeQualified) {
    const auto        c = rosetta::gen_detail::make_context<itp::Solver>("itest");
    const std::string out =
        rosetta::backend_registry().at("python")->render(c);

    EXPECT_NE(out.find("(const Eigen::Matrix<double, -1, 1> &)"), std::string::npos)
        << "the overload cast lost the Eigen:: qualification";
}

// A backend with no caster for the library skips those members rather than
// emitting a binding that throws at call time — the point of leaving `kind`
// "unknown". The ordinary member still binds, so this is a per-member skip and
// not the whole class dropping out.
TEST(InteropEigen, BackendsWithoutACasterSkipTheMembers) {
    const auto c = rosetta::gen_detail::make_context<itp::Solver>("itest");

    for (const char *lang : {"node", "wasm", "lua"}) {
        const std::string out = rosetta::backend_registry().at(lang)->render(c);
        EXPECT_EQ(out.find("\"solution\""), std::string::npos)
            << lang << " bound an Eigen-returning method it cannot marshal";
        EXPECT_EQ(out.find("\"setRhs\""), std::string::npos)
            << lang << " bound an Eigen-taking method it cannot marshal";
        EXPECT_NE(out.find("\"residual\""), std::string::npos)
            << lang << " dropped the ordinary method too";
    }
}
