// Copyright (c) fmaerten@gmail.com
// License: MIT

// Node (N-API) generation backend — *expanded* / self-contained variant,
// registered under the "node" target.
//
// Like python / wasm, this fully expands every field, method,
// constructor and enumerator into explicit N-API registrations from the IR,
// instead of emitting a thin auto_napi.cpp that re-runs the reflection walk
// (bind_napi<T>) at the target's compile time. The generated source includes
// only <napi.h>, the reflection-free <rosetta/runtime/node.h>, and the
// user's headers — no <experimental/meta>, no reflection — so it builds with an
// ordinary C++20 compiler.
//
// Unlike python/wasm (whose runtime is the third-party pybind11/embind header),
// N-API's marshalling layer is rosetta-provided, so this is the one expanded
// target that still needs rosetta's include dir on the path — but only for the
// reflection-free runtime/node.h, never the reflective headers.
//
// Implementation in inline/node.hxx. Reuses
// gen_detail::node_trampolines_of() (whose emitted trampolines compile against
// runtime/node.h's rosetta:: names) and qualify_std() from python_expanded.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct Node : Backend {
            void        emit(const GenContext &c) const override;
            std::string render(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/node.hxx"
