// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Opt-in registration of FOREIGN 2-D MATRIX types (Eigen::MatrixXd, a library's
// own dense grid) so they cross the binding boundary as an array of rows:
//
//   template <> struct rosetta::is_matrix<Eigen::MatrixXd> : std::true_type {};
//   template <> struct rosetta::matrix_cpp_name<Eigen::MatrixXd> {
//       static constexpr const char *value = "Eigen::MatrixXd";
//   };
//
// (or, manifest-driven: "matrices": [{ "type": "Eigen::MatrixXd" }] —
// rosetta_gen emits both specializations into the generated bindings.h.)
//
// This is rosetta::is_sequence one dimension up, and exists for the same
// reason: the marshalling layers know std::vector, and a 2-D type is not a
// sequence in any useful sense, so the backends with no caster for it (node,
// wasm, lua) skipped every member that named one. They now marshal it by COPY
// through a std::vector<std::vector<element>> boundary — scripts pass and
// receive an array of row arrays — while python / nanobind,
// where "interop" gives them a real caster, keep binding the type as numpy.
//
// A registered type must be default-constructible and provide value_type,
// rows(), cols(), resize(r, c) and operator()(i, j); the element must be
// arithmetic. As with sequences the copy is real, so a mutable `Mat&`
// parameter binds INPUT-ONLY: the adapter's local is a genuine lvalue, but
// in-place mutations are discarded.
//
// Rows come first and the boundary is row-major regardless of how the matrix
// stores itself, because operator()(i, j) is the only access the registration
// promises. A ragged incoming array is squared off to the first row's length —
// short rows keep whatever resize() left, extra columns are dropped.

#pragma once

#include <type_traits>

namespace rosetta {

    template <typename T> struct is_matrix : std::false_type {};

    // The EXACT C++ spelling of a registered matrix, for the types the composed
    // one gets wrong — same role, same default as rosetta::sequence_cpp_name:
    // absent, the adapter spells the type `Namespace::Template<value_type>`,
    // which is right for a one-parameter grid template and wrong for
    // Eigen::MatrixXd (really Matrix<double, -1, -1>, whose composed form
    // `Eigen::Matrix<double>` does not compile).
    template <typename T> struct matrix_cpp_name {};

} // namespace rosetta
