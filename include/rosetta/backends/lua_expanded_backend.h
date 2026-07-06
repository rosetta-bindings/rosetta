// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Lua (sol2) generation backend — *expanded* / self-contained variant.
//
// Fully expands every field, method, constructor and enumerator into explicit
// sol2 calls from the IR (GenContext) the driver already produced. The
// generated `auto_sol.cpp` includes only <sol/sol.hpp> plus the user's headers
// — no rosetta, no <experimental/meta>, no reflection — and builds with a
// stock C++17 compiler against any Lua 5.1–5.4 or LuaJIT.
//
// The output is a `require`-able C module: a shared library exposing
// `luaopen_<module>`, so scripts just do `local geom = require("geom")`.
// sol2's marshalling is a notably good match for the IR:
//   - a signature naming a std::vector gets a second, table-accepting overload
//     (sol::nested), so both a plain Lua table and another binding's container
//     userdata are accepted; a returned container is a reference-semantics
//     userdata (ipairs/#/[] work, mutations hit the C++ object);
//   - a Lua function converts to a std::function parameter natively (no
//     adapter like embind's emscripten::val shim);
//   - an lvalue-ref class return is pushed BY REFERENCE (no copy), so
//     non-copyable classes keep more surface than under pybind/embind;
//   - multiple inheritance is supported (sol::bases<A, B>), unlike embind.
//
// Registered under the "lua-expanded" target. Caveat: the generated file
// still `#include`s the bound headers, so a stock toolchain can build it only
// when *those headers are themselves stock C++* — i.e. annotations are supplied
// out of line (manifest "annotations": "...") rather than inline `[[=...]]`.
//
// Part of the generate pipeline (included by inline/generate.hxx after the
// shared render helpers and after python_expanded_backend.h, whose
// qualify_std() it reuses). The emit()/render() implementations live in
// inline/lua_expanded_backend.hxx.

#pragma once

namespace rosetta {
    namespace gen_detail {

        struct LuaExpandedBackend : Backend {
            void        emit(const GenContext &c) const override;
            std::string render(const GenContext &c) const override;
        };

    } // namespace gen_detail
} // namespace rosetta

#include "inline/lua_expanded_backend.hxx"
