// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Expanded (reflection-free) Java generation backend — declaration. Included by
// inline/generate.hxx after backends/java.h (reuses the shared Java wrapper /
// pom.xml / README rendering), backends/python.h (reuses qualify_std)
// and backends/csharp.h (reuses csx_double_lit), plus the shared render
// helpers. The emit() implementation lives in inline/java.hxx.
//
// Same outputs as the thin `java` backend, but the native shim it emits under
// <out_dir>/java/ fills the runtime registry with explicit
// member-pointer dispatch (<rosetta/runtime/java.h>) instead of a
// reflection walk — so it builds with a stock C++20 compiler, no clang-p2996.
// The generated Java sources / pom.xml are byte-identical to the thin backend's.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct Java : Backend {
            void        emit(const GenContext &c) const override;
            std::string render(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/java.hxx"
