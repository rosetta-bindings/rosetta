// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// A machine-readable account of what rosetta actually bound, and what it did
// not.
//
// rosetta's marshalling policy is deliberately conservative: a member a backend
// cannot represent is SKIPPED rather than bound into something that throws at
// call time. That is the right default — a skipped method is honest — but on its
// own it is silent. Today a user discovers that 12 of their 40 methods never
// made it by reading generated C++, and the failure looks identical to the
// method having never been asked for. The same shape recurs in the bug history:
// a request accepted and then quietly discarded (the abi3 wheel, the
// expose/extension key mismatch).
//
// This header turns every one of those decisions into a record, and generate()
// writes them to `<out_dir>/coverage.json`. A skip is still a skip; it is just
// no longer invisible, and a diff of coverage.json turns "a method silently
// stopped binding" into a reviewable line.
//
// Two stages produce records:
//
//   * REFLECTION — members the walk never handed to any backend: operators and
//     conversion functions (no bindable name), member templates, base overloads
//     hidden by a derived declaration. These are backend-independent and reach
//     the report through GenClass::dropped (see rosetta/walk.h drop_reason).
//
//   * BACKEND — members one target's gates rejected: an unmarshalable type, a
//     non-copyable by-value parameter, or an overload the target language cannot
//     express. Recorded here, keyed by target, because only the backend knows.
//
// The log is a process-global accumulated during generate(); generation is
// single-threaded, so it is not synchronized. Call reset() between runs (tests
// that render more than once must).
//
// NOT A STANDALONE HEADER. <rosetta/generate.h> includes it after the IR
// structs and before inline/generate.hxx, which is the only ordering in which
// both halves resolve: this header needs GenClass/GenMethod/GenField, and
// generate() needs to_json(). Including <rosetta/generate.h> — which every
// backend and test already does — is what makes `rosetta::coverage::` available.

#pragma once

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace rosetta::coverage {

    // ---------------------------------------------------------------------
    // Records
    // ---------------------------------------------------------------------

    /**
     * @brief One member a backend declined to bind.
     *
     * `reason` is a stable slug meant to be matched by tooling, not read as
     * prose ("overload_not_expressible", "unmarshalable_type", …); `detail` is
     * the human sentence. `signature` distinguishes members of an overload set,
     * which share `member`.
     */
    struct Skip {
        std::string target;    // backend lang ("python", "node")
        std::string scope;     // owning class, qualified ("geom::Model"); "" for a free function
        std::string member;    // "at"
        std::string signature; // "int (int, int) const", when the backend knows it
        std::string reason;    // machine-readable slug
        std::string detail;    // one human sentence
    };

    /**
     * @brief One member a backend did bind. Counting only skips cannot tell
     * "everything bound" apart from "the class never reached this backend", so
     * the report records both sides.
     */
    struct Bound {
        std::string target;
        std::string scope;
        std::string member;
        std::string signature;
    };

    struct Log {
        std::vector<Skip>  skips;
        std::vector<Bound> bound;
    };

    /** @brief The process-global log. Not synchronized — see the header note. */
    Log &log();

    /** @brief Drop everything recorded so far. */
    void reset();

    // ---------------------------------------------------------------------
    // Recording
    // ---------------------------------------------------------------------

    void note_bound(const char *target, const GenClass &k, const GenMethod &m);
    void note_bound_field(const char *target, const GenClass &k, const GenField &f);

    void note_skip(const char *target, const GenClass &k, const GenMethod &m, const char *reason,
                   std::string detail = {});
    void note_skip_field(const char *target, const GenClass &k, const GenField &f,
                         const char *reason, std::string detail = {});

    // ---------------------------------------------------------------------
    // Overload policy
    // ---------------------------------------------------------------------

    /**
     * @brief How a target language handles two methods that share a name.
     *
     * `native` — the framework dispatches on the argument types at call time
     * (pybind11 and nanobind build an overload set; jlcxx defers to Julia's
     * multiple dispatch; a TypeScript declaration merges). Every entry binds.
     *
     * `first_only` — methods are keyed by name and a name can be registered
     * once: embind's `.function`, N-API's property descriptors, the C#/Java op
     * tables, sol2's `c["name"] =` assignment, a REST route. Registering twice
     * is not an overload — it is a duplicate that overwrites, throws at module
     * init, or fails to compile. The first-declared entry binds and the rest are
     * recorded as skipped.
     */
    enum class overloads { native, first_only };

    /**
     * @brief Whether `target` should emit `m`, recording the decision.
     *
     * Backends call this as the first gate in their per-method loop, so the
     * dropped siblings of an overload set land in coverage.json instead of
     * disappearing. Returns true for every non-overloaded method, so it is safe
     * to call unconditionally.
     *
     * This decides only the OVERLOAD question. A backend's own marshalling gates
     * still run afterwards and should record their own rejections via
     * note_skip().
     */
    bool emit_overload(overloads policy, const char *target, const GenClass &k, const GenMethod &m);

    // ---------------------------------------------------------------------
    // Report
    // ---------------------------------------------------------------------

    /**
     * @brief The whole report as JSON: the reflection-stage drops read from
     * `classes`, plus everything recorded in the log, grouped by target.
     *
     * Shape (schema version 1):
     * {
     *   "rosetta_coverage": 1,
     *   "reflection": [ { "class": "...",
     *                     "dropped": [ {"member","signature","reason"} ] } ],
     *   "targets":    [ { "target": "...", "bound": N, "skipped": M,
     *                     "classes": [ { "class": "...",
     *                                    "bound":   [ {"member","signature"} ],
     *                                    "skipped": [ {"member","signature",
     *                                                  "reason","detail"} ] } ] } ]
     * }
     */
    std::string to_json(const std::vector<GenClass> &classes);

} // namespace rosetta::coverage

#include "inline/coverage.hxx"
