// Copyright (c) fmaerten@gmail.com
// License: MIT

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
// "skip, don't fail" contract. std::vector<T> members/returns cross as plain JS
// Arrays: the generated source specializes embind's BindingType to put them on
// the emval wire and emits a register_type<std::vector<T>>() per element type,
// rather than register_vector<T>, whose opaque M.vector_double handle class
// would make this the one backend where a sequence is not the host's own array.
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
