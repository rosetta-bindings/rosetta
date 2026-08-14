// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Expanded (reflection-free) C# generation backend — declaration. Included by
// inline/generate.hxx after csharp_backend.h (reuses the shared C# wrapper /
// .csproj / README rendering) and python_backend.h (reuses qualify_std),
// plus the shared render helpers. The emit() implementation lives in
// inline/csharp_backend.hxx.
//
// Same outputs as the thin `csharp` backend, but the native shim it emits under
// <out_dir>/csharp/ fills the runtime registry with explicit
// member-pointer dispatch (<rosetta/backends/csharp_runtime.h>) instead of a
// reflection walk — so it builds with a stock C++20 compiler, no clang-p2996.
// The generated C# wrapper / .csproj are byte-identical to the thin backend's.

#pragma once

namespace rosetta {
    namespace gen_detail {

        struct CSharpBackend : Backend {
            void        emit(const GenContext &c) const override;
            std::string render(const GenContext &c) const override;
        };

    } // namespace gen_detail
} // namespace rosetta

#include "inline/csharp_backend.hxx"
