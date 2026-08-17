// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Reflection-free N-API runtime for the "node" backend.
//
// The marshalling layer (to_napi / from_napi / Wrap / ctor_table / trampoline
// plumbing), with the per-member accessors keyed on *member and function
// pointers* (and a fixed-string name) rather than std::meta::info splices — as
// a removed reflection-driven counterpart once did. It includes no <experimental/meta>, so a generated auto_napi.cpp
// that uses it builds with an ordinary C++20 compiler — no clang-p2996, no
// reflection. (node-addon-api itself is, of course, still required, exactly as
// pybind11 is for the python target.)
//
// The names live in namespace `rosetta`, matching visitors/node.h, so the
// trampoline source emitted by gen_detail::node_trampolines_of() compiles
// against either runtime unchanged. The two headers are never included in the
// same TU (one per generated target), so the shared names never collide.
//
// This header holds the documented declarations (plus the small compile-time
// trait types the declarations need); the implementations live in
// inline/node.hxx, included at the bottom — the generate.h /
// inline/generate.hxx layout convention.

#pragma once

#include <cstddef>
#include <functional>
#include <napi.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace rosetta {

    // ---- Type classification (identical to node_visitor, reflection-free) ----

    template <typename T> struct is_std_vector : std::false_type {};
    template <typename U, typename A> struct is_std_vector<std::vector<U, A>> : std::true_type {};

    template <typename T> struct is_shared_ptr : std::false_type {};
    template <typename U> struct is_shared_ptr<std::shared_ptr<U>> : std::true_type {};

    // What an out-parameter adapter returns (see to_napi): the return value, if
    // any, followed by the out-parameters — handed to JS as an array.
    template <typename T> struct is_std_tuple : std::false_type {};
    template <typename... U> struct is_std_tuple<std::tuple<U...>> : std::true_type {};

    // ---- Compile-time helpers unique to the expanded runtime ----

    // A string usable as a non-type template parameter (C++20), so a field's
    // name can ride along into the read-only / range error message without
    // reflection.
    template <std::size_t N> struct fixed_str {
        char data[N]{};
        constexpr fixed_str(const char (&s)[N]) {
            for (std::size_t i = 0; i < N; ++i) {
                data[i] = s[i];
            }
        }
    };

    // Signature traits for a member- or free-function pointer: return type,
    // arity, and the I-th parameter type. Replaces the std::meta::parameters_of
    // / return_type_of queries the reflective visitor used.
    template <typename F> struct fn_traits;
    template <typename R, typename C, typename... A> struct fn_traits<R (C::*)(A...)> {
        using ret                          = R;
        static constexpr std::size_t arity = sizeof...(A);
        template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
    };
    template <typename R, typename C, typename... A> struct fn_traits<R (C::*)(A...) const> {
        using ret                          = R;
        static constexpr std::size_t arity = sizeof...(A);
        template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
    };
    template <typename R, typename... A> struct fn_traits<R (*)(A...)> {
        using ret                          = R;
        static constexpr std::size_t arity = sizeof...(A);
        template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
    };

    // ---- Forward declarations (mutually recursive with conversions) ----

    template <typename T, typename Tramp = T> class Wrap;

    /** @brief The persistent JS constructor for the wrapped class T. */
    template <typename T> Napi::FunctionReference &ctor_ref();

    // ---- Virtual-method trampoline support (verbatim from node_visitor) ----

    class NapiTrampoline {
      public:
        void __rosetta_set_self(Napi::Object self) {
            self_     = Napi::Weak(self);
            has_self_ = true;
        }
        bool         __rosetta_has_self() const { return has_self_ && !self_.IsEmpty(); }
        Napi::Object __rosetta_self() const { return self_.Value(); }

      private:
        Napi::ObjectReference self_;
        bool                  has_self_ = false;
    };

    /** @brief Bound prototype functions per class, for override detection. */
    template <typename T>
    std::unordered_map<std::string, Napi::FunctionReference> &napi_override_guard();

    /** @brief Parameterized-constructor table for the held type, keyed by arity. */
    template <typename T>
    std::unordered_map<std::size_t, std::function<T(const Napi::CallbackInfo &)>> &ctor_table();

    // ---- Type conversion (declarations; definitions in inline/node.hxx) ----

    /** @brief Convert a C++ value to a JS value (a class type is wrapped by copy). */
    template <typename T> Napi::Value to_napi(Napi::Env env, const T &v);

    /**
     * @brief Convert a JS value to a C++ value. Returns by value for scalars /
     * strings / vectors / enums, but by REFERENCE for wrapped class types: the
     * C++ object lives inside the JS object's Wrap, so handing out `T&` (a)
     * lets a bound function mutate the caller-visible object through `T&` /
     * out-parameters, and (b) avoids copying types whose copy is shallow or
     * deleted (e.g. pImpl facades — a by-value return would copy the pointer
     * and dangle it when the temporary is destroyed).
     */
    template <typename T> decltype(auto) from_napi(const Napi::Value &v);

    // ---- Callbacks: a JS function as a std::function parameter ----

    template <typename T> struct is_std_function : std::false_type {};
    template <typename R, typename... A>
    struct is_std_function<std::function<R(A...)>> : std::true_type {};

    /**
     * @brief Wrap a JS function in the std::function a bound signature asks for.
     *
     * A `std::function` parameter is the only way a callback crosses: a member
     * TEMPLATE (`map(F&&)`) has nothing to reflect on, so a bindable API spells
     * the callback out as a concrete type. This is the node half of what
     * rosetta_wx::make_fn does for wasm.
     *
     * TWO RULES the wrapper cannot enforce for you:
     *
     *   * The persisted reference makes the callback outlive the call, so a class
     *     that STORES the callback and fires it later still works. The reference
     *     is released when the last copy of the closure dies — which also means a
     *     callback stored forever in C++ keeps that JS function alive forever.
     *   * It is still only callable on the JS thread. Handing it to a C++ thread
     *     pool is undefined; N-API has no lock to take.
     */
    template <typename F> struct napi_fn_wrap;
    template <typename R, typename... A> struct napi_fn_wrap<std::function<R(A...)>> {
        static std::function<R(A...)> make(Napi::Function f);
    };

    template <typename F> F napi_make_fn(Napi::Function f) {
        return napi_fn_wrap<F>::make(f);
    }

    /** @brief Whether a JS subclass overrides the named bound method. */
    template <typename T> bool napi_is_overridden(Napi::Object self, const char *name);

    /** @brief Call the JS override when present, else the C++ base thunk. */
    template <typename T, typename Ret, typename Base, typename... Args>
    Ret napi_call_override(const NapiTrampoline &self, const char *name, Base base,
                           const Args &...args);

    /** @brief Call the JS override of a pure virtual; throws when absent. */
    template <typename T, typename Ret, typename... Args>
    Ret napi_call_override_pure(const NapiTrampoline &self, const char *name,
                                const Args &...args);

    /**
     * @brief Run `body` and turn any C++ exception into a JavaScript one.
     *
     * WHY EVERY ENTRY POINT NEEDS THIS. node-addon-api's own boundary wrapper
     * (`details::WrapCallback`) catches `Napi::Error` and nothing else. A bound
     * library that rejects bad input the ordinary C++ way — `throw
     * std::invalid_argument(...)` — therefore escapes past N-API into the C++
     * runtime, which calls std::terminate: the whole node process dies where
     * pybind11 and CxxWrap would both have handed the script a catchable error.
     * Nothing in the generated code can fix that, because the throw happens
     * inside the user's function; it has to be caught here, at the boundary.
     *
     * The mapping follows JavaScript's own vocabulary rather than inventing one:
     * `std::out_of_range` is a RangeError, `std::invalid_argument` and
     * `std::domain_error` are TypeErrors, anything else deriving from
     * std::exception is an Error carrying what(). A `Napi::Error` already on its
     * way out is left alone — the range and read-only setters throw those
     * deliberately.
     */
    template <typename F> decltype(auto) guard(Napi::Env env, F &&body);

    // ---- CRTP wrapper: accessors keyed on member/function pointers ----

    /**
     * @brief The N-API wrapper for one bound class. The wrapped object is held
     * by POINTER: either owned (allocated by this Wrap — the ordinary
     * `new Cls()` path) or aliased (a member object living inside ANOTHER
     * wrapped object — the member-object property path, in which case
     * `parent_` pins the owning JS object so the storage outlives every child
     * handle). Pointer storage is also what lets a non-default-constructible
     * class (GEO::MeshVertices, reachable only as `mesh.vertices`) be wrapped
     * at all.
     */
    template <typename T, typename Tramp> class Wrap : public Napi::ObjectWrap<Wrap<T, Tramp>> {
      public:
        Tramp &inner() { return *ptr_; }

        Wrap(const Napi::CallbackInfo &info);
        ~Wrap();

      private:
        /** @brief The constructor's real body, so it can run inside guard(). */
        void construct(const Napi::CallbackInfo &info);

      public:

        /** @brief Field getter (copies through to_napi). */
        template <auto MemPtr> Napi::Value get_field(const Napi::CallbackInfo &info);

        /**
         * @brief Member-object property getter: wrap the ADDRESS of the member
         * object in a fresh JS object of its own class, aliased (not owned),
         * with this JS object pinned as the parent — `mesh.vertices` hands out
         * the real MeshVertices living inside the mesh, valid as long as any
         * child handle is alive.
         */
        template <auto MemPtr> Napi::Value get_member_object(const Napi::CallbackInfo &info);

        /** @brief Field setter (copy-assigns through from_napi). */
        template <auto MemPtr>
        void set_field(const Napi::CallbackInfo &info, const Napi::Value &v);

        /** @brief Range-validating field setter (rosetta::range annotation). */
        template <auto MemPtr, fixed_str Name, double Lo, double Hi>
        void set_field_ranged(const Napi::CallbackInfo &info, const Napi::Value &v);

        /** @brief Setter stub for a read-only field: always throws. */
        template <auto MemPtr, fixed_str Name>
        void set_field_readonly(const Napi::CallbackInfo &info, const Napi::Value &v);

        /** @brief Instance-method thunk, keyed on the member-function pointer. */
        template <auto MFP> Napi::Value call_method(const Napi::CallbackInfo &info);

        /**
         * @brief Instance-method thunk for a method returning `T&` to a BOUND
         * class: hands out an aliased wrap of the referent, pinned to this
         * object — `facet_corners.attributes()` gives the store's own
         * AttributesManager, not a copy of it. The alternative, to_napi's
         * default, copy-assigns into a fresh wrapper, which is wrong for a
         * reference and does not compile at all for a non-copyable class.
         */
        template <auto MFP> Napi::Value call_method_alias(const Napi::CallbackInfo &info);

        /**
         * @brief Extension method: a FREE function whose first parameter is
         * `T&` (or `const T&`), exposed as an instance method — the wrapped
         * object is passed as the receiver and info[i] maps to parameter i+1.
         */
        template <auto FP> Napi::Value ext_method(const Napi::CallbackInfo &info);

        /** @brief Static-method thunk, keyed on the function pointer. */
        template <auto FP> static Napi::Value call_static(const Napi::CallbackInfo &info);

      private:
        Tramp                *ptr_   = nullptr;
        bool                  owned_ = false;
        Napi::ObjectReference parent_; // pins the owner while aliased

        // Shared ownership: set when this JS object was built from a
        // std::shared_ptr the C++ side handed out (to_napi's shared_ptr branch),
        // in which case ptr_ aliases into it and `owned_` stays false — the
        // reference count, not the destructor, decides when the object dies. The
        // third ownership mode, next to "owns a new'd object" and "aliases a
        // member of a pinned parent".
        std::shared_ptr<Tramp> shared_;

        template <auto FP, std::size_t... Is>
        Napi::Value ext_method_impl(const Napi::CallbackInfo &info, std::index_sequence<Is...>);

        template <auto MFP, std::size_t... Is>
        Napi::Value call_method_impl(const Napi::CallbackInfo &info, std::index_sequence<Is...>);

        template <auto MFP, std::size_t... Is>
        Napi::Value call_method_alias_impl(const Napi::CallbackInfo &info,
                                           std::index_sequence<Is...>);

        template <auto FP, std::size_t... Is>
        static Napi::Value call_static_impl(const Napi::CallbackInfo &info,
                                            std::index_sequence<Is...>);
    };

    // ---- Free-function entry, keyed on the function pointer ----

    template <auto FP, std::size_t... Is>
    Napi::Value napi_free_call(const Napi::CallbackInfo &info, std::index_sequence<Is...>);

    template <auto FP> Napi::Value napi_free_entry(const Napi::CallbackInfo &info);

    // ---- Enum object from an explicit name/value list (no reflection) ----

    inline Napi::Object
    make_enum(Napi::Env env,
              std::initializer_list<std::pair<const char *, long long>> values);

} // namespace rosetta

#include "inline/node.hxx"
