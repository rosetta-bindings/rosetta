// Copyright (c) fmaerten@gmail.com
// License: MIT

// HTML documentation backend — declaration. Emits a single self-contained
// `.html` API reference for the bound classes (an HTML companion to the Markdown
// backend). Part of the generate pipeline (included by inline/generate.hxx after
// the shared render helpers); the emit() implementation lives in
// inline/html.hxx. Not a standalone header — it relies on Backend /
// GenContext from <rosetta/generate.h> being already visible.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct Html : Backend {
            void        emit(const GenContext &c) const override;   // writes <out>/html/<lib>.html
            std::string render(const GenContext &c) const override; // the same document, as a string
        };

    } // namespace backend

    /// Reflection-driven HTML reference for one or more types, as a string
    /// (no files written). The pipeline-free entry point — pass a class/enum
    /// pack; `lib` is the document title.
    template <typename... Ts> std::string to_html(std::string lib = "doc");

} // namespace rosetta

#include "inline/html.hxx"
