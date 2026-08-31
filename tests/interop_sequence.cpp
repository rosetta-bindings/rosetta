// Copyright (c) fmaerten@gmail.com
// License: MIT

// Google Test suite for the flat-array ESCAPE HATCH on top of foreign-library
// interop: a concrete foreign type registered as a rosetta::is_sequence while
// "interop" is on for the library that owns it.
//
// Interop alone splits the backends in two: the Python family binds the type
// natively (numpy), everyone else SKIPS the member, because binding one that
// always throws would be worse. That leaves node / wasm / lua with nothing.
// Registering the concrete spelling as a sequence gives them the flat array
// they can actually marshal — without taking numpy away from the backends that
// have a caster, which is what the dual marking in the IR is for.
//
// Two things are being pinned here: the IR carries BOTH marks, and each backend
// picks the right one.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <cstddef>
#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>
#include <vector>

// Stand-in for a dense Eigen vector, declared the way the real one is (an alias
// for a specialization carrying more than its element) and with the container
// surface Eigen 3.4 exposes: value_type, size(), resize(), begin()/end().
namespace Eigen {
    template <typename T, int Rows, int Cols> struct Matrix {
        using value_type = T;

        std::size_t size() const { return data_.size(); }
        void        resize(std::size_t n) { data_.resize(n); }
        T          *begin() { return data_.data(); }
        T          *end() { return data_.data() + data_.size(); }
        const T    *begin() const { return data_.data(); }
        const T    *end() const { return data_.data() + data_.size(); }

        // The 2-D half of the same surface, as Eigen really carries it: one
        // template, two shapes, told apart by the specialization.
        std::size_t rows() const { return rows_; }
        std::size_t cols() const { return cols_; }
        void        resize(std::size_t r, std::size_t c) {
            rows_ = r;
            cols_ = c;
            data_.assign(r * c, T{});
        }
        T       &operator()(std::size_t i, std::size_t j) { return data_[i * cols_ + j]; }
        const T &operator()(std::size_t i, std::size_t j) const { return data_[i * cols_ + j]; }

    private:
        std::vector<T> data_;
        std::size_t    rows_ = 0;
        std::size_t    cols_ = 0;
    };
    using VectorXd = Matrix<double, -1, 1>;
    using MatrixXd = Matrix<double, -1, -1>;
} // namespace Eigen

namespace itp {
    class Solver {
    public:
        Solver() = default;

        Eigen::VectorXd solution() const { return {}; }
        void            setRhs(const Eigen::VectorXd &b) { rhs_ = b; }

        // The 2-D shape: a sequence in no useful sense, so it takes the matrix
        // registration instead — the array-of-rows boundary.
        Eigen::MatrixXd stiffness() const { return {}; }

        // Untouched by either mechanism: must keep binding exactly as before.
        double residual() const { return 0.0; }

    private:
        Eigen::VectorXd rhs_;
    };
} // namespace itp

template <> struct rosetta::binding_info<itp::Solver> {
    static constexpr const char *header = "Solver.h";
};

// What the manifest emits for `"interop": ["eigen"]` plus a "sequences" entry
// in its object form, { "type": "Eigen::VectorXd" }. The spelling has to be
// STATED: composing it from the template and the element would produce
// `Eigen::Matrix<double>`, which does not compile.
template <> struct rosetta::interop_enabled<rosetta::eigen_interop> : std::true_type {};
template <> struct rosetta::is_sequence<Eigen::VectorXd> : std::true_type {};
template <> struct rosetta::sequence_cpp_name<Eigen::VectorXd> {
    static constexpr const char *value = "Eigen::VectorXd";
};
// ... and `"matrices": [{ "type": "Eigen::MatrixXd" }]` for the 2-D one.
template <> struct rosetta::is_matrix<Eigen::MatrixXd> : std::true_type {};
template <> struct rosetta::matrix_cpp_name<Eigen::MatrixXd> {
    static constexpr const char *value = "Eigen::MatrixXd";
};

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

// Both marks survive into the IR, and the spelling is the registered one rather
// than the composed `Eigen::Matrix<double>` — which is the whole reason the
// concrete form exists.
TEST(InteropSequence, IrCarriesBothMarksWithTheStatedSpelling) {
    const auto c = rosetta::gen_detail::make_context<itp::Solver>("itest");
    ASSERT_EQ(c.classes.size(), 1u);
    const auto &k   = c.classes.front();
    const auto &sol = method_named(k, "solution");

    EXPECT_TRUE(sol.ret.is_sequence);
    EXPECT_EQ(sol.ret.interop, "eigen") << "the interop mark must survive alongside the sequence";
    EXPECT_EQ(sol.ret.seq_cpp, "Eigen::VectorXd");
    ASSERT_EQ(sol.ret.element.size(), 1u);
    EXPECT_EQ(sol.ret.element.front().kind, "number");
    // Still "unknown", so a backend that opted into NEITHER keeps skipping it.
    EXPECT_EQ(sol.ret.kind, "unknown");
    // The caster backends spell the type from these.
    EXPECT_EQ(sol.ret.object_qualified, "Eigen::Matrix<double, -1, 1>");
}

// The backends with no caster now BIND the members they used to skip, through
// the sequence adapter: the boundary is std::vector<double> and the adapter
// names the registered spelling.
TEST(InteropSequence, CasterlessBackendsBindThroughTheFlatAdapter) {
    const auto c = rosetta::gen_detail::make_context<itp::Solver>("itest");

    for (const char *lang : {"node", "wasm", "lua"}) {
        const std::string out = rosetta::backend_registry().at(lang)->render(c);
        EXPECT_NE(out.find("\"solution\""), std::string::npos)
            << lang << " skipped the returning method the escape hatch is for";
        EXPECT_NE(out.find("\"setRhs\""), std::string::npos)
            << lang << " skipped the taking method the escape hatch is for";
        EXPECT_NE(out.find("Eigen::VectorXd"), std::string::npos)
            << lang << " did not name the registered spelling in its adapter";
        EXPECT_NE(out.find("std::vector<double>"), std::string::npos)
            << lang << " did not marshal through the flat boundary vector";
        EXPECT_NE(out.find("\"residual\""), std::string::npos)
            << lang << " dropped the ordinary method";
    }
}

// The Python family keeps the caster: numpy in both directions, no adapter and
// no copied list. A dual-marked type must not cost them that.
TEST(InteropSequence, PythonFamilyKeepsTheCasterNotTheAdapter) {
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
        EXPECT_EQ(out.find("std::vector<double>"), std::string::npos)
            << cs.lang << " marshalled through the flat adapter instead of the caster";
    }
}

// The 2-D member takes the same split: an array of rows where there is no
// caster, numpy where there is one.
TEST(InteropSequence, MatrixSplitsTheSameWay) {
    const auto c = rosetta::gen_detail::make_context<itp::Solver>("itest");
    ASSERT_FALSE(c.classes.empty());
    const auto &st = method_named(c.classes.front(), "stiffness").ret;
    EXPECT_TRUE(st.is_matrix);
    EXPECT_FALSE(st.is_sequence);
    EXPECT_EQ(st.interop, "eigen");
    EXPECT_EQ(st.mat_cpp, "Eigen::MatrixXd");

    for (const char *lang : {"node", "wasm", "lua"}) {
        const std::string out = rosetta::backend_registry().at(lang)->render(c);
        EXPECT_NE(out.find("\"stiffness\""), std::string::npos)
            << lang << " skipped the matrix member";
        EXPECT_NE(out.find("std::vector<std::vector<double>>"), std::string::npos)
            << lang << " did not marshal the matrix as an array of rows";
    }
    for (const char *lang : {"python", "nanobind"}) {
        const std::string out = rosetta::backend_registry().at(lang)->render(c);
        EXPECT_NE(out.find("\"stiffness\""), std::string::npos)
            << lang << " skipped the matrix member";
        EXPECT_EQ(out.find("std::vector<std::vector<double>>"), std::string::npos)
            << lang << " flattened a matrix its caster handles as numpy";
    }
}

// TypeScript declares the flat shape, since that is what the node / wasm
// runtimes it describes actually hand out.
TEST(InteropSequence, TypescriptDeclaresTheFlatArray) {
    const auto c = rosetta::gen_detail::make_context<itp::Solver>("itest");
    // The .d.ts backend writes a file rather than rendering to a string; check
    // the type mapping directly, as the sequence suite does.
    using rosetta::backend::ts_type;
    ASSERT_FALSE(c.classes.empty());
    EXPECT_EQ(ts_type(method_named(c.classes.front(), "solution").ret, c), "number[]");
}
