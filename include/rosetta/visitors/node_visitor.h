// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// N-API backend: implements the rosetta::walk visitor concept.
//
// Provides:
//   - rosetta::to_napi / rosetta::from_napi — type conversions for scalars,
//     std::string, std::vector<U>, and nested user types (marshalled as
//     wrapped Napi objects via a per-type constructor reference)
//   - rosetta::Wrap<T> — CRTP Napi::ObjectWrap holding T + per-Fld/per-Fn
//     member-function templates whose addresses feed InstanceAccessor /
//     InstanceMethod / StaticMethod as NTTPs; its constructor dispatches by
//     argument count via a per-type constructor table
//   - rosetta::NapiVisitor<T> — visitor methods that push descriptors;
//     members using a type N-API cannot convert are skipped
//   - rosetta::bind_napi<T>(env, name) — entry point: runs the walk and
//     calls Wrap<T>::DefineClass
//
// The implementation lives in inline/node_visitor.hxx.

#pragma once

#include <array>
#include <cstdint>
#include <experimental/meta>
#include <functional>
#include <napi.h>
#include <memory>
#include <rosetta/walk.h>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rosetta {

    // Trampoline defaults to T (plain wrapper); the node backend passes a generated
    // trampoline subclass for classes with virtuals. The default lives here on the
    // declaration; the definition must NOT repeat it (one default per template, and
    // a mismatched 1-param decl would make the <T> call ambiguous).
    template <typename T, typename Trampoline = T>
    Napi::Function bind_napi(Napi::Env, const char *);

    // Build a frozen JS object exposing an enum's enumerators as numeric
    // constants ({ Red: 0, Green: 1, ... }). T must be an enumeration type.
    template <typename T> Napi::Object bind_napi_enum(Napi::Env);

    // Wrap a free function (named by its reflection F) as a JS function that
    // marshals arguments and the return value through to_napi/from_napi.
    template <std::meta::info F> Napi::Function bind_napi_function(Napi::Env, const char *);

}

#include "inline/node_visitor.hxx"
