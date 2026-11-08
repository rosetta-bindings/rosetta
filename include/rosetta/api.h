// Copyright (c) fmaerten@gmail.com
// License: MIT

// The public-API snapshot: the IR, reduced to what a caller depends on, as JSON.
//
// generate() writes it to `<out_dir>/api.json` on every run, next to
// coverage.json. Keep the file of each release (commit it, or attach it to the
// release) and `rosetta_gen --api-diff old/api.json new/api.json` reports what
// changed between the two — removed or renamed classes and members, changed
// signatures, narrowed ranges, renumbered enumerators — and which of those
// changes break existing callers. See <rosetta/api_diff.h> and docs/API_DIFF.md.
//
// The snapshot is TARGET-NEUTRAL: it is what reflection saw, before any backend
// decided what it could marshal. "Did python stop binding X?" is a question for
// coverage.json; "did the C++ API that every binding is built from change?" is
// this one.
//
// NOT A STANDALONE HEADER. <rosetta/generate.h> includes it after the IR structs;
// the definition (inline/api.hxx) is included after inline/generate.hxx, so it
// can use the IR accessors (qualified_of / exposed_of).

#pragma once

#include <string>
#include <vector>

namespace rosetta::api {

    /**
     * @brief The public API of one generation as JSON.
     *
     * Shape (schema version 1):
     * {
     *   "rosetta_api": 1,
     *   "version": "...",                       // GenerateOptions::version, may be ""
     *   "classes": [ { "name": "ns::Cls",       // qualified C++ name — the identity key
     *                  "expose": "Cls",         // the name scripts see
     *                  "bases": [...], "abstract": b, "final": b,
     *                  "default_constructible": b,
     *                  "constructors": [ { "params": [PARAM...] } ],
     *                  "fields":  [ { "name", "type", "readonly": b,
     *                                 "range": {"min","max"}?, "choices": [...]? } ],
     *                  "methods": [ { "name", "returns", "params": [PARAM...],
     *                                 "static": b, "const": b, "virtual": b,
     *                                 "pure": b, "extension": b } ] } ],
     *   "enums":     [ { "name", "expose", "underlying",
     *                    "values": [ {"name", "value"} ] } ],
     *   "functions": [ { "name", "qualified", "returns", "params": [PARAM...] } ]
     * }
     * PARAM = { "name", "type", "default": "<text>"?, "out": b }
     *
     * `default` is present only when the parameter has one; its value is the
     * default's source text when rosetta captured it, else "".
     */
    std::string to_json(const std::vector<GenClass> &classes, const std::vector<GenEnum> &enums,
                        const std::vector<GenFunction> &functions, const std::string &version);

} // namespace rosetta::api
