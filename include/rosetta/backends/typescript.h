// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// TypeScript generation backend — declaration. Part of the generate pipeline
// (included by inline/generate.hxx after the shared render helpers); the emit()
// implementation and any source templates live in
// inline/typescript.hxx. Not a standalone header — it relies on
// Backend / GenContext from <rosetta/generate.h> being already visible.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct TypeScript : Backend {
            void emit(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/typescript.hxx"
