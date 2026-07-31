// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Google Test suite for foreign 2-D matrices (rosetta::is_matrix, manifest
// "matrices").
//
// rosetta::is_sequence one dimension up: a library's own dense grid crosses the
// boundary by COPY through a std::vector<std::vector<element>> — an array of
// rows — so the backends with no caster for the type (node, wasm, lua) bind the
// members that name it instead of skipping them. The conversions go through
// rows()/cols()/operator()(i, j), the only access the registration promises.
//
// This suite covers the plain case: a container that belongs to no interop
// library, registered by TEMPLATE, so the spelling is composed. The dual
// interop case (Eigen::MatrixXd) lives in interop_sequence.cpp.
//
// Requires: -freflection -freflection-latest -fannotation-attributes

#include <cstddef>
#include <gtest/gtest.h>
#include <rosetta/generate.h>
#include <string>
#include <vector>

namespace mat {

    // A one-type-parameter grid — the shape the composed spelling
    // (Namespace::Template<element>) is right for.
    template <typename T> struct Grid {
        using value_type = T;

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
        std::size_t    rows_ = 0;
        std::size_t    cols_ = 0;
        std::vector<T> data_;
    };

    class Field {
    public:
        Field() = default;

        Grid<double> data() const { return weights; }
        void         setData(const Grid<double> &g) { weights = g; }
        double       norm() const { return 0.0; }

        Grid<double> weights;
    };

} // namespace mat

template <> struct rosetta::binding_info<mat::Field> {
    static constexpr const char *header = "Field.h";
};

// What the manifest's "matrices": ["mat::Grid"] emits into bindings.h.
template <typename T> struct rosetta::is_matrix<mat::Grid<T>> : std::true_type {};

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

// The IR marks the matrix, keeps `kind` "unknown" (so a backend that didn't opt
// in keeps skipping it — the is_pointer / is_sequence pattern) and records the
// element plus the spelling an adapter constructs.
TEST(Matrix, IrMarksTheMatrixAndComposesTheSpelling) {
    const auto c = rosetta::gen_detail::make_context<mat::Field>("mtest");
    ASSERT_EQ(c.classes.size(), 1u);
    const auto &k = c.classes.front();

    const auto &data = method_named(k, "data");
    EXPECT_TRUE(data.ret.is_matrix);
    EXPECT_FALSE(data.ret.is_sequence) << "a matrix is not a sequence";
    EXPECT_EQ(data.ret.kind, "unknown");
    EXPECT_EQ(data.ret.mat_cpp, "mat::Grid<double>");
    ASSERT_EQ(data.ret.element.size(), 1u);
    EXPECT_EQ(data.ret.element.front().kind, "number");

    ASSERT_EQ(method_named(k, "setData").params.size(), 1u);
    EXPECT_TRUE(method_named(k, "setData").params.front().type.is_matrix);

    // An ordinary member is untouched.
    EXPECT_EQ(method_named(k, "norm").ret.kind, "number");
}

// Every opted-in runtime backend binds both members through the copy adapter,
// with the array-of-rows boundary on the script side.
TEST(Matrix, RuntimeBackendsBindThroughTheRowArrayBoundary) {
    const auto c = rosetta::gen_detail::make_context<mat::Field>("mtest");

    for (const char *lang : {"python-expanded", "nanobind-expanded", "node-expanded",
                             "wasm-expanded", "lua-expanded"}) {
        const std::string out = rosetta::backend_registry().at(lang)->render(c);
        EXPECT_NE(out.find("\"data\""), std::string::npos) << lang << " skipped the matrix return";
        EXPECT_NE(out.find("\"setData\""), std::string::npos)
            << lang << " skipped the matrix parameter";
        EXPECT_NE(out.find("std::vector<std::vector<double>>"), std::string::npos)
            << lang << " did not marshal through the array-of-rows boundary";
        EXPECT_NE(out.find("mat::Grid<double>"), std::string::npos)
            << lang << " did not name the matrix in its adapter";
        EXPECT_NE(out.find("\"norm\""), std::string::npos) << lang << " dropped the ordinary method";
    }
}

// The conversions themselves: rows/cols off the incoming array (squared off to
// the first row, so a ragged one cannot run past the end) and a two-loop copy
// back out through operator()(i, j).
TEST(Matrix, AdapterConvertsBothDirections) {
    const auto        c = rosetta::gen_detail::make_context<mat::Field>("mtest");
    const std::string out =
        rosetta::backend_registry().at("python-expanded")->render(c);

    EXPECT_NE(out.find(".resize(mat0_r, mat0_c);"), std::string::npos) << out;
    EXPECT_NE(out.find("mat0(i, j) = arg0[i][j];"), std::string::npos) << out;
    EXPECT_NE(out.find("j < mat0_c && j < arg0[i].size()"), std::string::npos)
        << "a ragged incoming array must not be read past its row";
    EXPECT_NE(out.find("o[i][j] = r(i, j);"), std::string::npos) << out;
}

// A matrix-typed public field binds as a copying property, like a sequence one.
TEST(Matrix, FieldsBindAsCopyingProperties) {
    const auto        c = rosetta::gen_detail::make_context<mat::Field>("mtest");
    const std::string out =
        rosetta::backend_registry().at("python-expanded")->render(c);

    EXPECT_NE(out.find("def_property(\"weights\""), std::string::npos) << out;
}

// TypeScript declares the two-dimensional shape.
TEST(Matrix, TypescriptDeclaresAnArrayOfRows) {
    const auto c = rosetta::gen_detail::make_context<mat::Field>("mtest");
    using rosetta::gen_detail::ts_type;
    ASSERT_FALSE(c.classes.empty());
    EXPECT_EQ(ts_type(method_named(c.classes.front(), "data").ret, c), "number[][]");
}
