// Copyright (c) fmaerten@gmail.com
// License: MIT

// MCP (Model Context Protocol) backend — a stdio server an LLM agent drives.
//
// Built on the dynamic backend rather than beside it: the emitted tree carries
// the same TypeDesc / MetaClass tables (dynamic_source / dynamic_header), plus
// a ten-line main() that hands rosetta::dyn::registry() to the generic server
// in <rosetta/runtime/mcp.h>. Nothing per-class is generated for the protocol
// side at all — the tool set is fixed and the agent discovers the library by
// calling describe_class — so a new class in the manifest costs the server no
// code, only metadata.
//
// Registered under the "mcp" target. Stock C++20 on the target, like dynamic;
// the one extra dependency is nlohmann/json (found, or fetched by CMake).
//
// Part of the generate pipeline (included by inline/generate.hxx after
// backends/dynamic.h, whose emitters it reuses). See docs/MCP.md for the
// design; the implementation lives in inline/mcp.hxx.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct Mcp : Backend {
            void emit(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/mcp.hxx"
