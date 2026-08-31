// Copyright (c) fmaerten@gmail.com
// License: MIT

// Expanded (reflection-free) C# generation backend — declaration. Included by
// inline/generate.hxx after backends/csharp.h (reuses the shared C# wrapper /
// .csproj / README rendering) and backends/python.h (reuses qualify_std),
// plus the shared render helpers. The emit() implementation lives in
// inline/csharp.hxx.
//
// Same outputs as the thin `csharp` backend, but the native shim it emits under
// <out_dir>/csharp/ fills the runtime registry with explicit
// member-pointer dispatch (<rosetta/runtime/csharp.h>) instead of a
// reflection walk — so it builds with a stock C++20 compiler, no clang-p2996.
// The generated C# wrapper / .csproj are byte-identical to the thin backend's.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct CSharp : Backend {
            void        emit(const GenContext &c) const override;
            std::string render(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/csharp.hxx"
