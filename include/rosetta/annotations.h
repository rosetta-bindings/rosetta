// Copyright (c) fmaerten@gmail.com
// License: MIT

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

        // A std::vector<number> field drawn as a small line plot (sparkline)
        // instead of a list of numbers.
        struct plot_tag {
            bool operator==(const plot_tag &) const = default;
        };
        // A numeric field with a range, shown as a progress bar — a read-out
        // (completion, residual, quality), not an editor.
        struct progress_tag {
            bool operator==(const progress_tag &) const = default;
        };

        inline constexpr spin_tag      spin{};
        inline constexpr slider_tag    slider{};
        inline constexpr checkbox_tag  checkbox{};
        inline constexpr textfield_tag textfield{};
        inline constexpr color_tag     color{};
        inline constexpr multiline_tag multiline{};
        inline constexpr radio_tag     radio{};
        inline constexpr file_tag      file{};
        inline constexpr plot_tag      plot{};
        inline constexpr progress_tag  progress{};

    } // namespace widget

    // =====================================================================
    // Semantics — what a value means
    // =====================================================================

    /**
     * @brief Physical unit of a numeric field or a method's return value.
     *
     * Rendered as a suffix by UI backends, in docstrings and reference pages,
     * and reported to MCP clients by describe_class — so an agent reasons in
     * metres, not in "a double".
     *
     *   [[= rosetta::unit{"m"}]] double length;
     */
    struct unit {
        const char *text;
        consteval unit(const char *t) : text(std::define_static_string(t)) {}
        bool operator==(const unit &) const = default;
    };

    /**
     * @brief Default for one PARAMETER of a method, named by its identifier.
     *
     * C++ default arguments are not reflectable as values (P2996 reports that
     * a default exists, not what it is), so a binding cannot always offer
     * them. This states it: bindings make the parameter optional, the dynamic
     * model fills it in, an MCP client may omit it. Repeat it for several
     * parameters.
     *
     *   [[= rosetta::default_value{"dt", 0.001}]]
     *   double simulate(double seconds, double dt);
     *
     * Numbers and booleans take the numeric form; a string default uses the
     * string form (`default_value{"mode", "fast"}`).
     */
    struct default_value {
        const char *param;
        double      number = 0;
        const char *text   = nullptr; // non-null: a string default
        consteval default_value(const char *p, double v)
            : param(std::define_static_string(p)), number(v) {}
        consteval default_value(const char *p, int v)
            : param(std::define_static_string(p)), number(v) {}
        consteval default_value(const char *p, const char *s)
            : param(std::define_static_string(p)), text(std::define_static_string(s)) {}
        bool operator==(const default_value &) const = default;
    };

    /**
     * @brief A sample value (for a field) or a sample call (for a method), as
     * JSON text. Shown to MCP clients and in OpenAPI / reference pages — an
     * example is often the fastest way for an agent or a reader to get it.
     *
     *   [[= rosetta::example{"{\"seconds\": 2.0, \"dt\": 0.001}"}]]
     */
    struct example {
        const char *json;
        consteval example(const char *j) : json(std::define_static_string(j)) {}
        bool operator==(const example &) const = default;
    };

    // =====================================================================
    // Validation — enforced by the dynamic core and the validating setters
    // =====================================================================

    /** @brief One-sided lower bound (inclusive). Pair with max, or use range. */
    struct min {
        double value;
        bool   operator==(const min &) const = default;
    };

    /** @brief One-sided upper bound (inclusive). */
    struct max {
        double value;
        bool   operator==(const max &) const = default;
    };

    /**
     * @brief Allowed size of a string or a sequence: lo <= size() <= hi.
     *
     *   [[= rosetta::length{1, 64}]] std::string name;
     */
    struct length {
        std::size_t lo;
        std::size_t hi;
        bool        operator==(const length &) const = default;
    };

    /**
     * @brief ECMAScript regular expression a string field must match in full.
     *
     *   [[= rosetta::regex{"#[0-9a-fA-F]{6}"}]] std::string colour;
     */
    struct regex {
        const char *pattern;
        consteval regex(const char *p) : pattern(std::define_static_string(p)) {}
        bool operator==(const regex &) const = default;
    };

    /**
     * @brief A precondition the annotated member must satisfy, as a small
     * expression over the object's fields, its nullary methods and — on a
     * method — the call's parameters:
     *
     *   [[= rosetta::requires_{"dt < period() / 10", "dt too large for this rod"}]]
     *   double simulate(double seconds, double dt);
     *
     *   [[= rosetta::requires_{"hi > lo"}]] double hi;   // checked with the new value
     *
     * Grammar: numbers, true/false, identifiers (field or parameter), name()
     * (a nullary method), + - * /, comparisons, && || !, parentheses. Checked
     * by the dynamic core (so by the MCP server, scripts and dynamic UIs) and
     * written into the reference documentation everywhere else. Spelled with a
     * trailing underscore because `requires` is a C++ keyword; the side-car key
     * is "requires".
     */
    struct requires_ {
        const char *expr;
        const char *message;
        consteval requires_(const char *e, const char *m = "")
            : expr(std::define_static_string(e)), message(std::define_static_string(m)) {}
        bool operator==(const requires_ &) const = default;
    };

    // =====================================================================
    // Behaviour — what calling a method does
    // =====================================================================

    /**
     * @brief The method changes or discards state in a way the user may not
     * want undone. MCP clients see `destructive` and must pass
     * `"confirm": true` to call it; UI backends ask before running it.
     */
    struct destructive {
        bool operator==(const destructive &) const = default;
    };

    /**
     * @brief Ask before running the method, with this question. Implies
     * `destructive`.
     *
     *   [[= rosetta::confirm{"Discard the current mesh?"}]] void reset();
     */
    struct confirm {
        const char *question;
        consteval confirm(const char *q) : question(std::define_static_string(q)) {}
        bool operator==(const confirm &) const = default;
    };

    /**
     * @brief The method takes noticeable time. Agents are told to batch such
     * calls in a script; UI backends show a busy indicator while it runs.
     */
    struct expensive {
        bool operator==(const expensive &) const = default;
    };

    /**
     * @brief Keep a public member out of every binding — for state that must
     * stay public in C++ (ABI, a friend that cannot be one) but is not API.
     */
    struct hidden {
        bool operator==(const hidden &) const = default;
    };

    // =====================================================================
    // Lifecycle
    // =====================================================================

    /** @brief Still bound, but on its way out; `use` names the replacement. */
    struct deprecated {
        const char *use;
        consteval deprecated(const char *u = "") : use(std::define_static_string(u)) {}
        bool operator==(const deprecated &) const = default;
    };

    /** @brief The version that introduced the member, e.g. "1.2". */
    struct since {
        const char *version;
        consteval since(const char *v) : version(std::define_static_string(v)) {}
        bool operator==(const since &) const = default;
    };

    // =====================================================================
    // UI layout and editing
    // =====================================================================

    /** @brief Increment of a spin box / slider for a numeric field. */
    struct step {
        double value;
        bool   operator==(const step &) const = default;
    };

    /** @brief Digits shown after the decimal point. */
    struct decimals {
        int  count;
        bool operator==(const decimals &) const = default;
    };

    /**
     * @brief The field spans orders of magnitude (a tolerance from 1e-10 to
     * 1e-6): a slider moves along log10 of the value. Needs a positive range.
     */
    struct log_scale {
        bool operator==(const log_scale &) const = default;
    };

    /** @brief Section of the property panel the field belongs to. */
    struct group {
        const char *name;
        consteval group(const char *n) : name(std::define_static_string(n)) {}
        bool operator==(const group &) const = default;
    };

    /** @brief Position in the property panel (lower first; default: declaration order). */
    struct order {
        int  rank;
        bool operator==(const order &) const = default;
    };

    /**
     * @brief Editable only while a condition on the object holds — same
     * expression grammar as requires_:
     *
     *   [[= rosetta::enabled_if{"preserveShape"}]] double tangentWeight;
     */
    struct enabled_if {
        const char *expr;
        consteval enabled_if(const char *e) : expr(std::define_static_string(e)) {}
        bool operator==(const enabled_if &) const = default;
    };

    /** @brief Shown only while a condition on the object holds. */
    struct visible_if {
        const char *expr;
        consteval visible_if(const char *e) : expr(std::define_static_string(e)) {}
        bool operator==(const visible_if &) const = default;
    };

} // namespace rosetta
