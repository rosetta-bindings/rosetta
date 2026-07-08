// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// =============================================================================
// rosetta/annotate.h
//
// Out-of-line annotations: keep your headers clean and describe the same
// metadata (doc / range / readonly / combobox / label / button / widget) in a
// side-car JSON file that is baked into the program at compile time.
//
// The JSON is parsed at *compile time* and merged with whatever inline P3394
// annotations the member already carries -- so the two sources concatenate, and
// a class with no inline annotations and no side-car is simply un-annotated.
// Nothing here turns JSON into real P3394 annotations (that needs token
// injection, P3294, which clang-p2996 lacks); instead the parsed entries join
// the annotation pack at the single choke point in rosetta::walk().
//
// JSON schema (object keyed by member identifier):
//
//   {
//     "title": { "doc": "The widget title", "label": "Title" },
//     "count": { "doc": "Visible items", "range": [0, 100], "widget": "slider" },
//     "id":    { "readonly": true },
//     "mode":  { "combobox": ["fast", "slow"] },
//     "run":   { "doc": "Run the thing", "button": "Run" }
//   }
//
// Wiring: add an "annotations" field to the class entry in manifest.json;
// rosetta_gen bakes an ann_json_source<T> specialization into the generated
// bindings.h. The user's header and source stay free of annotation wiring.
// See docs/OUT_OF_LINE_ANNOTATIONS.md and docs/ANNOTATIONS.md. (This header
// only provides the customization point, the parser, and the walk()-time
// merge; it never needs #embed -- the bytes are baked at generation time.)
//
// Note: the minimal parser takes string values literally (no escape
// processing) and parses numbers as plain decimals with optional exponent --
// enough for the annotation schema, not a general-purpose JSON library.
//
// The implementations live in inline/annotate.hxx, included at the bottom.
// =============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <experimental/meta>
#include <rosetta/annotations.h>
#include <string>
#include <string_view>
#include <vector>

namespace rosetta {

    namespace detail {
        // Backing storage for a type's baked JSON bytes. rosetta_gen specializes
        // this per type with the side-car contents (as a std::to_array<char>).
        template <class T> inline constexpr std::array<char, 1> ann_storage = {'\0'};
    } // namespace detail

    // Customization point. Specialized (per type) by the generated bindings.h to
    // view the baked JSON bytes. The primary template is the "no side-car" case:
    // an empty source parses to an empty table, i.e. no annotations.
    template <class T> constexpr std::string_view ann_json_source = std::string_view{};

    namespace ann_json {

        // ---- parsed representation (all constexpr-friendly, no allocation) ----

        struct choice_list {
            std::string_view items[combobox::MAX]{};
            std::size_t      count = 0;
        };

        struct member_ann {
            std::string_view name{};
            bool             has_doc      = false;
            std::string_view doc{};
            bool             has_range    = false;
            double           rmin         = 0;
            double           rmax         = 0;
            bool             has_readonly = false;
            bool             readonly_on  = false;
            bool             has_combobox = false;
            choice_list      combo{};
            bool             has_label  = false; // "label": "EPS"
            std::string_view label{};
            bool             has_button = false; // "button": "Run" (methods)
            std::string_view button{};
            bool             has_widget = false; // "widget": "slider" | "spin" |
            std::string_view widget{};           //   "checkbox" | "textfield" |
                                                 //   "color" | "multiline" |
                                                 //   "radio" | "file"
        };

        inline constexpr std::size_t MAX_MEMBERS = 256;

        struct table {
            member_ann  items[MAX_MEMBERS]{};
            std::size_t count = 0;
            bool        ok    = true; // false on malformed input
        };

        /**
         * @brief Parse a side-car JSON source into the table above, at compile
         * time. An empty source yields an empty (valid) table; malformed input
         * sets `ok = false`. Unknown keys are skipped, never fatal.
         */
        consteval table parse(std::string_view src);

    } // namespace ann_json

    namespace detail {

        /**
         * @brief JSON-sourced annotations for one member name, as reflections
         * of constants — directly spliceable as NTTPs, same shape as the
         * entries std::meta::constant_of() yields for inline annotations.
         */
        template <class T>
        consteval std::vector<std::meta::info> json_annotations_for(std::string_view member);

        /**
         * @brief Inline annotations (normalized to constant reflections) ++
         * JSON ones. Inline come first, so when a visitor reads "the first /
         * last of kind A" the precedence is well-defined; doc-style kinds that
         * a backend chooses to render cumulatively simply see both.
         */
        template <class T>
        consteval std::vector<std::meta::info> merged_annotations(std::meta::info member);

        /**
         * @brief Compile-time guard: every JSON key must name a real member of
         * T, so a renamed/typo'd field fails the build instead of silently
         * losing data. Returns "" when valid, otherwise a message naming the
         * offending key (used as a P2741 constexpr static_assert message in
         * bindings.h).
         */
        template <class T> consteval std::string ann_keys_error();

    } // namespace detail

} // namespace rosetta

#include "inline/annotate.hxx"
