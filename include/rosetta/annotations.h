// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Shared annotation types used by every backend (pybind11, N-API, ...).
//
// Each annotation must be a structural / NTTP-eligible type. The `doc`
// constructor routes the literal through std::meta::define_static_string
// to get a pointer with linkage (a plain literal's address is not
// NTTP-eligible — see DETAILS.md).
//
// Build flag required: -fannotation-attributes

#pragma once

#include <cstddef>
#include <experimental/meta>

namespace rosetta {

    /**
     * @brief Documentation string annotation. Can be attached to types, members, or function
     * parameters.
     */
    struct doc {
        const char *text;
        consteval doc(const char *t) : text(std::define_static_string(t)) {}
        bool operator==(const doc &) const = default;
    };

    /**
     * @brief Numeric range annotation for function parameters. Can be used by backends to generate
     * validation or UI widgets.
     */
    struct range {
        double min;
        double max;
        bool   operator==(const range &) const = default;
    };

    /**
     * @brief Read-only annotation for members. Can be used by backends to generate
     * read-only UI widgets.
     */
    struct readonly {
        bool operator==(const readonly &) const = default;
    };

    /**
     * @brief Synthesized (not user-written) marker that walk<T>() injects into a
     * method's annotation pack when its surviving reflection is virtual. Lets
     * backends tell a virtual / overriding method apart from a plain one — e.g.
     * to emit a pybind11 trampoline so a Python subclass can override it.
     *
     * Detect with rosetta::ann::has<rosetta::virtual_spec>(Anns...); read the
     * flags with rosetta::ann::get_or<rosetta::virtual_spec>({}, Anns...).
     *
     * `pure` is true for a pure-virtual declaration (= 0); `overrides` is true
     * when the method overrides a base virtual. Note: after override dedup the
     * surviving reflection is the most-derived one, so a concrete override
     * reports pure == false even though the base declaration was pure.
     */
    struct virtual_spec {
        bool pure      = false;
        bool overrides = false;
        bool operator==(const virtual_spec &) const = default;
    };

    /**
     * @brief Drop-down / choice-list annotation for string-like members. Each
     * choice literal is routed through std::define_static_string so the
     * annotation stays NTTP-eligible.
     *
     * Usage:
     *   [[= rosetta::combobox{{"red", "green", "blue"}}]]
     *   std::string color;
     *
     * The choice list is capped at MAX entries. Past that, a combobox is the
     * wrong widget — switch to a search/autocomplete.
     */
    struct combobox {
        static constexpr std::size_t MAX = 16;
        const char                  *choices[MAX]{};
        std::size_t                  count = 0;

        constexpr combobox() = default;

        template <std::size_t N> consteval combobox(const char *const (&arr)[N]) : count(N) {
            static_assert(N <= MAX, "rosetta::combobox: too many choices (max 16)");
            for (std::size_t i = 0; i < N; ++i) {
                choices[i] = std::define_static_string(arr[i]);
            }
        }

        bool operator==(const combobox &) const = default;
    };

    /**
     * @brief Display-name override for UI backends. When attached to a
     * member or method, replaces the C++ identifier in the rendered
     * label. The identifier is still the lookup key used by
     * setField / getField / invoker — only the visible text changes.
     *
     * Usage:
     *   [[= rosetta::label{"Full name"}]]
     *   std::string name;
     */
    struct label {
        const char *text;
        consteval label(const char *t) : text(std::define_static_string(t)) {}
        bool operator==(const label &) const = default;
    };

    /**
     * @brief Action-button annotation for methods. The label overrides the
     * default "Call" / method-name text on the action button rendered by
     * a UI backend. An empty label means "use the method's identifier."
     *
     * Usage:
     *   [[= rosetta::button{"Reset"}]]
     *   void clear();
     */
    struct button {
        const char *label;
        consteval button() : label(std::define_static_string("")) {}
        consteval button(const char *l) : label(std::define_static_string(l)) {}
        bool operator==(const button &) const = default;
    };

    /**
     * @brief Widget hint annotations. Orthogonal to validation annotations
     * like `range`: a backend uses these to pick an editor when more than
     * one would be valid for the field's type.
     *
     * Usage:
     *   [[ = rosetta::range{0, 150}, = rosetta::widget::slider ]]
     *   int age;
     *
     * Each tag is an inline constexpr instance so it can be written
     * without braces in the annotation, matching `rosetta::moc::slot`.
     */
    namespace widget {

        struct spin_tag {
            bool operator==(const spin_tag &) const = default;
        };
        struct slider_tag {
            bool operator==(const slider_tag &) const = default;
        };
        struct checkbox_tag {
            bool operator==(const checkbox_tag &) const = default;
        };
        struct textfield_tag {
            bool operator==(const textfield_tag &) const = default;
        };
        // A string field holding a "#rrggbb" hex color — rendered as a color
        // picker / swatch by the UI backends.
        struct color_tag {
            bool operator==(const color_tag &) const = default;
        };
        // A string field edited as a multi-line text area.
        struct multiline_tag {
            bool operator==(const multiline_tag &) const = default;
        };
        // Render the field's combobox{} choices as a radio-button group
        // instead of a drop-down (pair it with rosetta::combobox).
        struct radio_tag {
            bool operator==(const radio_tag &) const = default;
        };
        // A string field holding a file path — text entry plus a browse
        // button opening the platform file dialog.
        struct file_tag {
            bool operator==(const file_tag &) const = default;
        };

        inline constexpr spin_tag      spin{};
        inline constexpr slider_tag    slider{};
        inline constexpr checkbox_tag  checkbox{};
        inline constexpr textfield_tag textfield{};
        inline constexpr color_tag     color{};
        inline constexpr multiline_tag multiline{};
        inline constexpr radio_tag     radio{};
        inline constexpr file_tag      file{};

    } // namespace widget

} // namespace rosetta
