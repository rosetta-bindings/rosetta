// Copyright (c) fmaerten@gmail.com
// License: MIT

#pragma once

// =============================================================================
// rosetta/mini_moc.h
//
// A header-only, moc-less reimagining of Qt's signals / slots / properties on
// top of C++26 reflection (P2996) + annotations (P3394).
//
// Requires a toolchain with reflection + annotation-attributes. Tested with the
// Bloomberg clang-p2996 fork.
//
// Notes vs. real Qt:
//  * Token injection (P3294) isn't in clang-p2996 yet, so we cannot auto-emit
//    `name()/setName()` accessors into the class body. Instead, callers reach
//    properties through `get<"name">(obj)` / `set<"name">(obj, v)` — same
//    semantics (equality-gated, fires NOTIFY), just outside the class.
//  * `connect<"sig","slot">(sender, receiver)` is compile-time-checked:
//    wrong name -> static_assert, wrong arg types -> template error.
//
// What you use:
//  * Annotations  : slot, property{...}            (mark members)
//  * Free funcs   : connect<...>, get<...>, set<...>
// A signal needs NO annotation: any `Signal<Args...>` data member IS a signal,
// recognized by its type via reflection. You never call its methods directly —
// connect through connect<>, and emission happens when set<> fires a property's
// NOTIFY. The Signal<...> machinery and the ScopedConnection RAII handle are
// treated as implementation detail and defined in inline/mini_moc.hxx, not here.
//
// See tests/moc.cpp
//
// =============================================================================

#include <cstddef>
#include <experimental/meta>
#include <functional>
#include <list>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace rosetta::moc {

    /**
     * fixed_string: a simple wrapper to allow string literals as NTTPs for annotation parameters.
     * The annotation machinery requires that the parameter be a pointer to static storage with
     * linkage, so we route the string literals through define_static_string to ensure they meet
     * that requirement. The view() method provides a convenient way to get a std::string_view of
     * the fixed_string, which is what the rest of the code uses for comparisons and lookups. We
     * also define an equality operator for fixed_string to allow for easy comparison in
     * static_asserts and other compile-time checks.
     * Note that fixed_string is a simple wrapper around a char array, and it doesn't manage memory
     * or provide any dynamic features. It's purely a compile-time utility for working with string
     * literals in the context of annotations and NTTPs.
     */
    template <std::size_t N> struct fixed_string {
        char data[N]{};
        consteval fixed_string(const char (&s)[N]) {
            for (std::size_t i = 0; i < N; ++i) {
                data[i] = s[i];
            }
        }
        constexpr std::string_view view() const { return {data, N - 1}; }
        bool                       operator==(const fixed_string &) const = default;
    };
    template <std::size_t N> fixed_string(const char (&)[N]) -> fixed_string<N>;

    /**
     * slot_tag is an empty struct used as an annotation tag to mark methods as slots. The
     * operator== is defaulted to allow for easy comparison in static_asserts and other compile-time
     * checks. This tag is used in the find_tagged function to identify which members of a class are
     * annotated as slots. By defining this tag as an empty struct, we can use it purely as a marker
     * without any associated data, and the defaulted equality operator allows us to compare it
     * easily when searching for annotated members.
     *
     * Signals need no equivalent tag: a signal is any `Signal<Args...>` data member, recognized by
     * its type in detail::find_signal — so it carries no annotation (see inline/mini_moc.hxx).
     */
    struct slot_tag {
        bool operator==(const slot_tag &) const = default;
    };

    inline constexpr slot_tag slot{};

    /**
     * property: an annotation struct used to mark fields as properties. It contains the name of the
     * property and an optional notify signal name. The operator== is defaulted to allow for easy
     * comparison in static_asserts and other compile-time checks. This annotation is used in the
     * find_property_field function to identify which fields of a class are annotated as properties.
     * By defining this struct with a constructor that takes the property name and an optional
     * notify signal name, we can easily annotate fields with the necessary metadata for property
     * access and change notification. The defaulted equality operator allows us to compare property
     * annotations easily when searching for annotated fields. Note that the property annotation is
     * designed to be used on fields, and the find_property_field function assumes that the
     * annotation is on the field itself, not on the getter/setter. This design choice simplifies
     * the implementation and is consistent with the idea of a "property" as a field with some extra
     * metadata. If we wanted to support annotations on getters/setters, we would need to look at
     * member functions as well and figure out which one is the "main" one for the property, which
     * would add complexity to the implementation.
     */
    struct property {
        const char *name;
        const char *notify;
        consteval property(const char *n, const char *nf = "")
            : name(std::define_static_string(n)), notify(std::define_static_string(nf)) {}
        bool operator==(const property &) const = default;
    };

    // Signal<...> is the one machinery type a user spells out; a data member of
    // this type IS a signal (no annotation needed — detail::find_signal matches
    // it by type). It is forward-declared here; the complete definition (along
    // with ScopedConnection, the RAII handle returned by Signal::scoped_connect
    // and normally bound with `auto`) lives in inline/mini_moc.hxx, included at
    // the bottom of this header. That include is required regardless: a
    // Signal<...> data member needs the complete type, which a forward
    // declaration alone cannot provide.
    template <class... Args> class Signal;

    /**
     * connect<"sig","slot">(sender, receiver): a function template that connects a signal from the
     * sender to a slot on the receiver. It uses compile-time reflection to find the matching members:
     * the signal is the sender's `Signal<...>` data member of that name (matched by type, no
     * annotation), and the slot is the receiver's [[=slot]]-tagged method of that name. If the
     * specified signal or slot is not found, it triggers a static_assert with an appropriate error
     * message. If both are found, it connects the signal to the slot by creating a lambda that
     * captures the receiver and calls the slot with the arguments from the signal. This function
     * provides a compile-time-checked way to connect signals and slots, ensuring that the specified
     * signal and slot exist and have compatible argument types. The use of fixed_string for the
     * signal and slot names allows us to use string literals as template parameters, which are then
     * used in the reflection queries to find the corresponding members.
     * Note that the actual connection is done by calling the connect method on the signal member of
     * the sender, passing a lambda that captures the receiver and calls the slot member of the
     * receiver. This means that the signal and slot members must be callable (e.g., the signal
     * member should be of type Signal<Args...> and the slot member should be a callable that can
     * accept the same arguments).
     *
     * Returns the Signal::Id of the freshly created connection, so the caller can later tear it down
     * with `(sender.theSignal).disconnect(id)`.
     */
    template <fixed_string Sig, fixed_string Slot, class S, class R>
    auto connect(S &sender, R &receiver);

    /**
     * get<"prop">(obj): read access to a property. It uses compile-time reflection to find the
     * field of the object that is annotated with the specified property name. If the property is
     * not found or if the annotation is not present, it triggers a static_assert with an
     * appropriate error message. If the property is found, it returns a const reference to the
     * field. This function provides a way to access properties that are annotated with the property
     * annotation, allowing us to read their values using a simple get function. The use of
     * fixed_string for the property name allows us to use string literals as template parameters,
     * which are then used in the reflection queries to find the corresponding field.
     * Note that this function assumes that the property annotation is on the field itself, and it
     * does not support properties that are defined through getters/setters. This design choice
     * simplifies the implementation and is consistent with the idea of a "property" as a field with
     * some extra metadata. If we wanted to support properties defined through getters/setters, we
     * would need to look at member functions as well and figure out which one is the "main" one for
     * the property, which would add complexity to the implementation.
     */
    template <fixed_string Name, class T> auto const &get(T const &obj);

    /**
     * set<"prop">(obj, value): write access to a property. It uses compile-time reflection to find
     * the field of the object that is annotated with the specified property name. If the property
     * is not found or if the annotation is not present, it triggers a static_assert with an
     * appropriate error message. If the property is found, it sets the field to the provided value.
     * This function provides a way to modify properties that are annotated with the property
     * annotation, allowing us to write their values using a simple set function. The use of
     * fixed_string for the property name allows us to use string literals as template parameters,
     * which are then used in the reflection queries to find the corresponding field.
     */
    template <fixed_string Name, class T, class V> void set(T &obj, V &&value);

} // namespace rosetta::moc

#include "inline/mini_moc.hxx"
