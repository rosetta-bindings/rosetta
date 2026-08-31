// Copyright (c) fmaerten@gmail.com
// License: MIT

// One consteval walk over T's reflected members. Each field/method is
// passed to the visitor together with its *full annotation pack* as
// non-type template parameters; the visitor decides which annotations
// it cares about — there is no per-shape entry-point in the walker.
//
// Visitor concept — `consteval`-template members:
//
//   v.template field          <Fld,  Anns...>(name);
//   v.template method_instance<Fn,   Anns...>(name);
//   v.template method_static  <Fn,   Anns...>(name);
//   v.template constructor    <Ctor, Anns...>();      // optional
//
// `constructor` is optional: the walk only calls it when the visitor
// defines it (detected with `requires`), so backends that don't model
// construction need not implement it. `Ctor` is the `std::meta::info`
// of a public, non-copy/move constructor.
//
// `Fld` / `Fn` are `std::meta::info` NTTPs. `Anns...` is a pack of
// annotation values (`auto...`) — each entry can be any structural
// annotation type (rosetta::doc, rosetta::range, rosetta::readonly, or
// future kinds like widget, alias, deprecated). `name` is a
// `const char*` to static storage (via define_static_string).
//
// Helpers `rosetta::ann::has<A>(Anns...)` and
// `rosetta::ann::get_or<A>(fallback, Anns...)` let visitors query the
// pack without writing the fold/if-constexpr boilerplate themselves.
//
// This header declares the public surface; the definitions live in
// inline/walk.hxx (included at the bottom).

#pragma once

#include <experimental/meta>
#include <rosetta/annotate.h>
#include <rosetta/annotations.h>
#include <type_traits>
#include <utility>

namespace rosetta {

    // -------- annotation pack helpers --------
    //
    // Both helpers take the annotation pack as runtime arguments (still
    // constant expressions because each Anns value flows through unchanged).
    // Call style at the visitor:
    //     ann::has<readonly>(Anns...)
    //     ann::get_or<doc>(doc{""}, Anns...).text
    namespace ann {

        /**
         * @brief Check if an annotation of type A is present in the pack.
         */
        template <typename A> consteval bool has(auto... anns);

        /**
         * @brief Get the first annotation of type A in the pack, or return a fallback value if absent.
         */
        template <typename A> consteval A get_or(A fallback, auto... anns);

    } // namespace ann

    // -------- what the walk left behind --------
    //
    // A member the walk could not hand to a visitor. These never reach the IR,
    // so no backend can report them and their absence from a binding is
    // otherwise only discoverable by noticing the call is missing. The coverage
    // report (rosetta/coverage.h) turns each one into a line naming the member
    // and the reason.
    enum class drop_reason {
        no_identifier,     // operator==, operator[], operator T — no bindable name
        function_template, // member template: no fixed parameter pack to splice
        hidden_by_derived, // base overload hidden by a derived declaration of the name
    };

    struct member_drop {
        std::meta::info member;
        drop_reason     reason;
    };

    /**
     * @brief A member_drop with its names already resolved to static strings.
     *
     * The reflection has to be turned into text INSIDE a consteval context —
     * `std::meta::display_string_of` and `define_static_string` are consteval,
     * and a `template for` loop variable is not usable as a constant expression
     * in the call. So detail::member_drop_texts() does the conversion during the
     * walk and hands back plain `const char*`s that runtime code can just copy.
     */
    struct drop_text {
        const char *member;
        const char *signature;
        const char *reason;
    };

    // Stable, machine-readable spelling of a drop reason ("no_identifier", …),
    // used as the `reason` field in the emitted coverage JSON.
    constexpr const char *drop_reason_name(drop_reason r) {
        switch (r) {
        case drop_reason::no_identifier:
            return "no_identifier";
        case drop_reason::function_template:
            return "function_template";
        case drop_reason::hidden_by_derived:
            return "hidden_by_derived";
        }
        return "unknown";
    }

    // Keep regular and static methods; drop constructors/destructors and
    // the compiler-generated copy/move specials.
    consteval bool is_exportable_member_function(std::meta::info fn);

    // Expose user-callable constructors: default and parameterized ones, but
    // not copy/move (those are value-passing, not distinct entry points) nor
    // constructor templates (their parameters aren't a fixed pack to splice)
    // nor deleted ones.
    consteval bool is_exportable_constructor(std::meta::info fn);

    /**
     * @brief Walk over reflected members of T and pass them to the visitor.
     */
    template <typename T, typename Visitor> void walk(Visitor &v);

    /**
     * @brief Is `Fn` the FIRST member function named `Fn` that walk<T> emits?
     *
     * The compile-time visitors (node, wasm, C#, Java, REST, Qt, QML) register a
     * method under its name — in a property table, an op map, a URL route — where
     * a name can only appear once. walk<T> now hands them every overload, so such
     * a visitor guards its entry point with
     *
     *     if constexpr (!first_overload<T, Fn>()) return;
     *
     * to keep the first-declared entry and ignore the rest. It is a `consteval`
     * counterpart to GenMethod::overload_index == 0, needed because a visitor
     * sees one member at a time and cannot know about its siblings.
     *
     * Visitors whose framework dispatches on argument types (pybind11, nanobind,
     * jlcxx) must NOT use this — for them every overload is meant to bind.
     */
    template <typename T, std::meta::info Fn> consteval bool first_overload();

} // namespace rosetta

#include "inline/walk.hxx"
