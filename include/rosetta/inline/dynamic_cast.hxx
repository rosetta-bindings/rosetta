// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Any <-> C++ conversion for the generated thunks.
//
// These templates are why an emitted thunk is one line. Without them the
// dynamic backend would have to spell a conversion per (member, type) pair —
// which is precisely the per-backend marshalling duplication the dynamic model
// exists to delete (docs/MAIN-TODO.md §3). Here the conversion is written once,
// generically, and every backend that speaks Any inherits it.
//
// Coverage is deliberately the same set the dynamic backend's type gate lets
// through: bool, arithmetic, enum, std::string, std::vector<U> of those, and
// bound class types by value / reference / pointer. A type outside that set
// never reaches these templates — the emitter records a coverage skip and emits
// a null thunk instead, keeping rosetta's skip-don't-emit-something-that-throws
// policy.
//
// Included by <rosetta/dynamic.h>; not standalone.

#pragma once

#include <type_traits>
#include <utility>

namespace rosetta::dyn {

    namespace cast_detail {

        template <class T> struct is_std_vector : std::false_type {};
        template <class T, class A> struct is_std_vector<std::vector<T, A>> : std::true_type {};

        template <class T> inline constexpr bool is_vector_v = is_std_vector<T>::value;

        // A "value type" for our purposes: cv- and ref-stripped.
        template <class T> using bare = std::remove_cv_t<std::remove_reference_t<T>>;

        [[noreturn]] inline void bad(const char *want, const Any &got) {
            throw Error(std::string("cannot convert ") +
                        (got.type() ? got.type()->spelling : kind_name(got.kind())) + " to " +
                        want);
        }

    } // namespace cast_detail

    // -------- Any -> C++ ---------------------------------------------------

    /**
     * @brief Read `v` as T, converting through the canonical representation.
     *
     * Numeric narrowing is permitted and silent (double -> int truncates), the
     * same latitude every scripting binding gives; the alternative — rejecting
     * 3.0 for an `int` parameter because the host language has one number type
     * — makes the dynamic path unusable from Lua and JavaScript.
     */
    template <class T> T value_cast(const Any &v) {
        using U = cast_detail::bare<T>;

        if constexpr (std::is_same_v<U, bool>) {
            switch (v.kind()) {
            case Kind::boolean:
                return v.as_bool();
            case Kind::number:
            case Kind::enum_:
                return v.as_number() != 0;
            default:
                cast_detail::bad("bool", v);
            }
        } else if constexpr (std::is_enum_v<U>) {
            if (v.kind() != Kind::enum_ && v.kind() != Kind::number) {
                cast_detail::bad("an enumeration", v);
            }
            return static_cast<U>(v.as_int());
        } else if constexpr (std::is_arithmetic_v<U>) {
            switch (v.kind()) {
            case Kind::number:
            case Kind::enum_:
                return static_cast<U>(v.as_number());
            case Kind::boolean:
                return static_cast<U>(v.as_bool() ? 1 : 0);
            default:
                cast_detail::bad("a number", v);
            }
        } else if constexpr (std::is_same_v<U, std::string>) {
            if (v.kind() != Kind::string) {
                cast_detail::bad("std::string", v);
            }
            return v.as_string();
        } else if constexpr (cast_detail::is_vector_v<U>) {
            if (v.kind() != Kind::vector) {
                cast_detail::bad("a sequence", v);
            }
            U out;
            out.reserve(v.as_list().size());
            for (const Any &e : v.as_list()) {
                out.push_back(value_cast<typename U::value_type>(e));
            }
            return out;
        } else if constexpr (std::is_pointer_v<U>) {
            using P = std::remove_pointer_t<U>;
            if (v.kind() != Kind::object) {
                cast_detail::bad("an object pointer", v);
            }
            return static_cast<U>(static_cast<P *>(v.as_object().ptr));
        } else {
            // A bound class, taken BY VALUE: copies out of the handle.
            if (v.kind() != Kind::object || !v.as_object().ptr) {
                cast_detail::bad("an object", v);
            }
            return *static_cast<U *>(v.as_object().ptr);
        }
    }

    /**
     * @brief Read `v` as a reference to a bound class — the no-copy path.
     *
     * Every `T&` / `const T&` parameter goes through here, which is what lets a
     * NON-COPYABLE class still be passed around dynamically (GenParam::is_ref
     * makes the same distinction on the compile-time side).
     */
    template <class T> T &ref_cast(const Any &v) {
        if (v.kind() != Kind::object || !v.as_object().ptr) {
            cast_detail::bad("an object reference", v);
        }
        return *static_cast<cast_detail::bare<T> *>(v.as_object().ptr);
    }

    // -------- C++ -> Any ---------------------------------------------------

    /**
     * @brief Box a returned value, canonicalizing it for its TypeDesc.
     *
     * A class return is COPIED to the heap and owned by the resulting Any —
     * correct for a by-value return, wrong for a reference return, which is why
     * the emitter uses borrow_any() for those instead.
     */
    template <class T> Any make_any(const TypeDesc *t, T &&x) {
        using U = cast_detail::bare<T>;

        if constexpr (std::is_same_v<U, bool>) {
            return Any::boolean(x);
        } else if constexpr (std::is_enum_v<U>) {
            return Any::enumeration(static_cast<long long>(x), t);
        } else if constexpr (std::is_integral_v<U>) {
            return Any::integer(static_cast<long long>(x), t);
        } else if constexpr (std::is_floating_point_v<U>) {
            return Any::real(static_cast<double>(x), t);
        } else if constexpr (std::is_convertible_v<U, std::string> &&
                             !std::is_class_v<U>) {
            return Any::text(std::string(x), t); // const char*
        } else if constexpr (std::is_same_v<U, std::string>) {
            return Any::text(std::forward<T>(x), t);
        } else if constexpr (cast_detail::is_vector_v<U>) {
            std::vector<Any> out;
            out.reserve(x.size());
            const TypeDesc *et = t ? t->element : nullptr;
            for (auto &e : x) {
                out.push_back(make_any(et, e));
            }
            return Any::list(std::move(out), t);
        } else if constexpr (std::is_pointer_v<U>) {
            using P = std::remove_pointer_t<U>;
            // A raw pointer return is BORROWED: rosetta has no way to know the
            // callee handed over ownership, and guessing wrong either leaks or
            // double-frees. The caller pins the parent if it needs to.
            return Any::object(const_cast<std::remove_const_t<P> *>(x),
                               t ? t->cls : nullptr, {}, t);
        } else {
            auto *heap = new U(std::forward<T>(x));
            return Any::object(heap, t ? t->cls : nullptr,
                               std::shared_ptr<void>(heap, [](void *p) { delete static_cast<U *>(p); }),
                               t);
        }
    }

    /**
     * @brief Box a reference return WITHOUT copying, optionally pinned.
     *
     * `owner` is the receiver's own owner: passing it makes the returned handle
     * keep the parent alive for as long as the child is held, which is the
     * missing piece behind "wasm returns raw pointers with no parent pin"
     * (docs/MAIN-TODO.md §2). Pass {} for a genuinely unowned reference.
     */
    template <class T>
    Any borrow_any(const TypeDesc *t, T &x, std::shared_ptr<void> owner = {}) {
        using U = cast_detail::bare<T>;
        return Any::object(const_cast<U *>(std::addressof(x)), t ? t->cls : nullptr,
                           std::move(owner), t);
    }

} // namespace rosetta::dyn
