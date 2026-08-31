// Copyright (c) fmaerten@gmail.com
// License: MIT

// OpenAPI 3.1 generation backend — declaration. Part of the generate pipeline
// (included by inline/generate.hxx after the shared render helpers); the emit()
// implementation and any source templates live in inline/openapi.hxx.
// Not a standalone header — it relies on Backend / GenContext from
// <rosetta/generate.h> being already visible.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct OpenApi : Backend {
            void emit(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/openapi.hxx"
