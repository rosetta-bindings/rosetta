// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// JSON (nlohmann) serialization via reflection — a rosetta::walk visitor.
//
// Unlike a code-generation backend, this is a *runtime* serializer: it walks a
// type's public data members and (de)serializes them with no per-type code.
// One header serializes every reflected class.
//
// Provides:
//   - rosetta::to_json<T>(const T&)      -> nlohmann::json
//   - rosetta::from_json<T>(const json&) -> T
//
// Behaviour:
//   - Only *public* data members are visited (the surface every rosetta
//     backend sees); types whose state is private round-trip as `{}`.
//   - Nested reflected classes and std::vector<…> recurse automatically.
//   - Enums (de)serialize by enumerator *name* (e.g. "Surface").
//   - `from_json<T>` requires T (and any nested type) to be default-constructible.
//
// The implementation lives in inline/json.hxx.

#pragma once

#include <experimental/meta>
#include <nlohmann/json.hpp>
#include <rosetta/walk.h>
#include <string>
#include <type_traits>
#include <vector>

namespace rosetta {

    template <typename T> nlohmann::json to_json(const T &);
    template <typename T> T             from_json(const nlohmann::json &);

} // namespace rosetta

#include "inline/json.hxx"
