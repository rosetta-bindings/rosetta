// Copyright (c) fmaerten@gmail.com
// License: MIT

// Python (pybind11) generation backend — *expanded* / self-contained variant.
//
// Unlike Python, which emits a thin `auto_pybind.cpp` that re-runs the
// reflection walk (bind_pybind<T>) at the *target's* compile time and so needs
// a C++26 / P2996 compiler to build, this backend fully expands every field,
// method, constructor and enumerator into explicit pybind11 calls from the IR
// (GenContext) the driver already produced. The generated `auto_pybind.cpp`
// therefore includes only <pybind11/...> plus the user's headers — no rosetta,
// no <experimental/meta>, no reflection — and builds with a stock C++17
// compiler (clang / gcc / MSVC) + pybind11.
//
// Registered under the "python" target. Caveat: the generated file
// still `#include`s the bound headers, so a stock toolchain can build it only
// when *those headers are themselves stock C++* — i.e. annotations are supplied
// out of line (manifest "annotations": "...") rather than inline `[[=...]]`.
//
// Part of the generate pipeline (included by inline/generate.hxx after the
// shared render helpers and after backends/python.h, whose trampoline helpers
// this backend reuses). The emit()/render() implementations live in
// inline/python.hxx.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct Python : Backend {
            void        emit(const GenContext &c) const override;
            std::string render(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/python.hxx"
