// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Pybind11 backend: implements the rosetta::walk visitor concept.
//
// Provides:
//   - rosetta::PybindVisitor<T> — visitor methods emitting pybind11 calls
//     (fields, instance/static methods, and one py::init per constructor)
//   - rosetta::bind_pybind<T>(module, py_name) — entry point: declares the
//     class, runs the walk, and registers a default ctor as a fallback
//
// The implementation lives in inline/python_visitor.hxx.

#pragma once

#include <experimental/meta>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <rosetta/walk.h>
#include <string>
#include <type_traits>

namespace py = pybind11;

namespace rosetta {

    // Trampoline defaults to T (plain py::class_<T>); the python backend passes a
    // generated Py_T subclass for classes with virtuals. The default lives here on
    // the declaration; the definition must NOT repeat it (one default per template,
    // and a mismatched 1-param decl would make the <T> call ambiguous). Trailing
    // `Bases...` are the (bound) C++ base classes registered with py::class_ so a
    // derived instance is accepted where a base pointer/reference is expected.
    template <typename T, typename Trampoline = T, typename... Bases>
    void bind_pybind(py::module_ &, const char *);

    // Register an enum type as a py::enum_ (its enumerators become module-level
    // constants via export_values). T must be an enumeration type.
    template <typename T> void bind_pybind_enum(py::module_ &, const char *);
}

#include "inline/python_visitor.hxx"
