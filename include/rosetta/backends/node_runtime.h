// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Reflection-free N-API runtime for the "node-expanded" backend.
//
// This is the stock-C++ counterpart of <rosetta/visitors/node_visitor.h>: the
// same marshalling layer (to_napi / from_napi / Wrap / ctor_table /
// trampoline plumbing), but with the per-member accessors keyed on *member and
// function pointers* (and a fixed-string name) instead of std::meta::info
// splices. It includes no <experimental/meta>, so a generated auto_napi.cpp
// that uses it builds with an ordinary C++20 compiler — no clang-p2996, no
// reflection. (node-addon-api itself is, of course, still required, exactly as
// pybind11 is for the python-expanded target.)
//
// The names live in namespace `rosetta`, matching node_visitor.h, so the
// trampoline source emitted by gen_detail::node_trampolines_of() compiles
// against either runtime unchanged. The two headers are never included in the
// same TU (one per generated target), so the shared names never collide.
//
// This header holds the documented declarations (plus the small compile-time
// trait types the declarations need); the implementations live in
// inline/node_runtime.hxx, included at the bottom — the generate.h /
// inline/generate.hxx layout convention.

#pragma once

#include <cstddef>
#include <functional>
#include <napi.h>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace rosetta {

    // ---- Type classification (identical to node_visitor, reflection-free) ----

    template <typename T> struct is_std_vector : std::false_type {};
    template <typename U, typename A> struct is_std_vector<std::vector<U, A>> : std::true_type {};

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

    // ---- Type conversion (declarations; definitions in inline/node_runtime.hxx) ----

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

        template <auto FP, std::size_t... Is>
        Napi::Value ext_method_impl(const Napi::CallbackInfo &info, std::index_sequence<Is...>);

        template <auto MFP, std::size_t... Is>
        Napi::Value call_method_impl(const Napi::CallbackInfo &info, std::index_sequence<Is...>);

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

#include "inline/node_runtime.hxx"
