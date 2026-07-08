// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Julia (CxxWrap / jlcxx) generation backend — *expanded* / self-contained
// variant.
//
// Unlike JuliaBackend, which emits a thin `auto_jlcxx.cpp` that re-runs the
// reflection walk (bind_julia<T>) at the *target's* compile time and so needs
// the C++26 / P2996 toolchain — and its libc++, whose gaps are why the thin
// backend must skip `std::vector` — this backend fully expands every field,
// method, constructor and enumerator into explicit jlcxx calls from the IR
// (GenContext) the driver already produced. The generated `auto_jlcxx.cpp`
// includes only <jlcxx/...> plus the user's headers — no rosetta, no
// reflection — and builds with a stock C++17 compiler against the stock
// libc++/libstdc++, so <jlcxx/stl.hpp> compiles and **std::vector members,
// parameters and returns are bound** (the thin backend's biggest gap).
//
// Surface (mirrors the python-expanded feature set, spelled the Julia way):
//   - fields    -> getter `name(obj)` + mutating setter `name!(obj, v)`;
//                  `readonly` drops the setter; `range` validates on set;
//                  gated on the IR's copyability flags like python-expanded.
//   - methods   -> `name(obj, args...)` with exact-signature static_cast
//                  disambiguation; statics become module-level functions.
//   - ctors     -> `.constructor<exact spellings...>()`, synthetic default
//                  when the type is default-constructible; skipped for an
//                  abstract class.
//   - enums     -> jlcxx bits type + one module constant per enumerator.
//   - vectors   -> scalar/string element vectors ride CxxWrap's StdVector;
//                  a second ArrayRef-taking overload is added per vector
//                  parameter so a plain Julia `Vector` works too (Julia's
//                  multiple dispatch picks the right one); vectors of bound
//                  classes are registered via jlcxx::stl::apply_stl.
//   - extension methods -> the free function is bound as the instance method
//                  (jlcxx passes the object as the first Julia argument).
//   - inheritance -> single bound base via jlcxx::SuperType + julia_base_type
//                  (jlcxx is single-inheritance, like embind).
//   - free functions -> module-level methods.
//   Callbacks (std::function parameters) are skipped: jlcxx has no automatic
//   Julia-closure -> std::function conversion (pybind11/functional has no
//   jlcxx equivalent).
//
// Registered under the "julia-expanded" target. Caveat: the generated file
// still `#include`s the bound headers, so a stock toolchain can build it only
// when *those headers are themselves stock C++* — i.e. annotations are
// supplied out of line (manifest "annotations": "...").
//
// Part of the generate pipeline (included by inline/generate.hxx after the
// shared render helpers and after python_expanded_backend.h, whose
// qualify_std() it reuses). The emit()/render() implementations live in
// inline/julia_expanded_backend.hxx.

#pragma once

namespace rosetta {
    namespace gen_detail {

        struct JuliaExpandedBackend : Backend {
            void        emit(const GenContext &c) const override;
            std::string render(const GenContext &c) const override;
        };

    } // namespace gen_detail
} // namespace rosetta

#include "inline/julia_expanded_backend.hxx"
