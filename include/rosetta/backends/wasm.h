// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// WebAssembly (emscripten/embind) generation backend — *expanded* /
// self-contained variant, registered under the "wasm" target.
//
// Like the python backend, this fully expands every field, method,
// constructor and enumerator into explicit embind calls from the IR the driver
// already produced, instead of emitting a thin `auto_emscripten.cpp` that
// re-runs the reflection walk (bind_wasm<T>) at the target's compile time. The
// generated source includes only <emscripten/bind.h> plus the user's headers —
// no rosetta, no <experimental/meta>, no reflection — so it builds with a
// *stock* emsdk rather than a reflection-aware fork.
//
// Members whose signature carries a type embind cannot marshal (e.g. a
// std::function parameter) are skipped rather than emitted, matching rosetta's
// "skip, don't fail" contract. std::vector<T> members/returns are supported via
// emitted register_vector<T>() registrations.
//
// Implementation in inline/wasm.hxx. Relies on Backend /
// GenContext from <rosetta/generate.h> and on gen_detail::qualify_std() from
// backends/python.h (both already visible when generate.hxx includes
// this file).

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct Wasm : Backend {
            void        emit(const GenContext &c) const override;
            std::string render(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/wasm.hxx"
