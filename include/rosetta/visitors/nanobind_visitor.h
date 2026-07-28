// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Nanobind backend: implements the rosetta::walk visitor concept.
//
// The leaner, faster sibling of <rosetta/visitors/python_visitor.h>. nanobind
// (https://github.com/wjakob/nanobind) is by pybind11's author and exposes a
// near-identical surface, so the visitor maps almost 1:1 — the differences are
// def_property{,_readonly} -> def_prop_rw / def_prop_ro, py::value_error ->
// nb::value_error, and the module macro NB_MODULE. Output is a smaller, faster
// extension with lower per-binding overhead.
//
// Provides:
//   - rosetta::NanobindVisitor<T> — visitor methods emitting nanobind calls
//   - rosetta::bind_nanobind<T>(module, name) — declare class, walk, default ctor
//   - rosetta::bind_nanobind_enum<T>(module, name)
//
// Virtual methods are bound as ordinary methods (callable from Python); Python
// subclasses overriding C++ virtuals (nanobind NB_TRAMPOLINE) is not yet wired
// — see the python backend for the trampoline pattern this would mirror.
//
// The implementation lives in inline/nanobind_visitor.hxx.

#pragma once

// nanobind splits its casters one per header, where pybind11 ships them all in
// <pybind11/stl.h>. Pull the same coverage the python visitor gets for free,
// plus the smart pointers (pybind11 handles those in its core): without the
// matching header a bound signature still COMPILES and then throws "Unable to
// convert function return value to a Python type" at call time — e.g. a factory
// returning std::shared_ptr<Iface>, a std::variant field, or a
// std::map<std::string, std::string> options argument. Each header only defines
// type casters, so the ones a module never exercises cost nothing.
#include <experimental/meta>
#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/vector.h>
#include <rosetta/walk.h>
#include <string>
#include <type_traits>

namespace nb = nanobind;

namespace rosetta {

    template <typename T> void bind_nanobind(nb::module_ &, const char *);

    // Register an enum type as an nb::enum_ (enumerators accessed as Name.Value).
    template <typename T> void bind_nanobind_enum(nb::module_ &, const char *);
}

#include "inline/nanobind_visitor.hxx"
