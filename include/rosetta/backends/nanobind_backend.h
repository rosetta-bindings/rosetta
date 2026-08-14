// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Nanobind generation backend — *expanded* / self-contained variant, registered
// under the "nanobind" target.
//
// The reflection-free counterpart of the "nanobind" target (and the nanobind
// sibling of "python"): every field, method, constructor and
// enumerator is fully expanded into explicit nanobind calls from the IR, so the
// generated auto_nanobind.cpp includes only <nanobind/...> plus the user's
// headers — no rosetta, no <experimental/meta>, no reflection. Because nanobind
// is built for ordinary toolchains, this is the most natural stock build of the
// lot: a plain C++17 compiler + the pip `nanobind` package, no clang-p2996.
//
// Implementation in inline/nanobind_backend.hxx. Reuses qualify_std()
// from python_backend.h.

#pragma once

namespace rosetta {
    namespace gen_detail {

        struct NanobindBackend : Backend {
            void        emit(const GenContext &c) const override;
            std::string render(const GenContext &c) const override;
        };

    } // namespace gen_detail
} // namespace rosetta

#include "inline/nanobind_backend.hxx"
