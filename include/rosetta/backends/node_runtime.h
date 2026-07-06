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
        using ret                         = R;
        static constexpr std::size_t arity = sizeof...(A);
        template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
    };
    template <typename R, typename C, typename... A> struct fn_traits<R (C::*)(A...) const> {
        using ret                         = R;
        static constexpr std::size_t arity = sizeof...(A);
        template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
    };
    template <typename R, typename... A> struct fn_traits<R (*)(A...)> {
        using ret                         = R;
        static constexpr std::size_t arity = sizeof...(A);
        template <std::size_t I> using arg = std::tuple_element_t<I, std::tuple<A...>>;
    };

    // ---- Forward declarations (mutually recursive with conversions) ----

    template <typename T, typename Tramp = T> class Wrap;
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

    template <typename T>
    inline std::unordered_map<std::string, Napi::FunctionReference> &napi_override_guard() {
        static std::unordered_map<std::string, Napi::FunctionReference> m;
        return m;
    }

    template <typename T>
    std::unordered_map<std::size_t, std::function<T(const Napi::CallbackInfo &)>> &ctor_table() {
        static std::unordered_map<std::size_t, std::function<T(const Napi::CallbackInfo &)>> table;
        return table;
    }

    // ---- Type conversion helpers (verbatim from node_visitor) ----

    template <typename T> Napi::Value to_napi(Napi::Env env, const T &v) {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<U, std::string>) {
            return Napi::String::New(env, v);
        } else if constexpr (std::is_same_v<U, bool>) {
            return Napi::Boolean::New(env, v);
        } else if constexpr (std::is_floating_point_v<U> || std::is_integral_v<U>) {
            return Napi::Number::New(env, static_cast<double>(v));
        } else if constexpr (is_std_vector<U>::value) {
            Napi::Array arr = Napi::Array::New(env, v.size());
            for (std::size_t i = 0; i < v.size(); ++i) {
                arr.Set(static_cast<uint32_t>(i), to_napi(env, v[i]));
            }
            return arr;
        } else if constexpr (std::is_enum_v<U>) {
            return Napi::Number::New(
                env, static_cast<double>(static_cast<std::underlying_type_t<U>>(v)));
        } else if constexpr (std::is_class_v<U>) {
            Napi::Object obj = ctor_ref<U>().New({});
            Wrap<U>::Unwrap(obj)->inner = v;
            return obj;
        } else {
            static_assert(sizeof(T) == 0, "to_napi: unsupported type");
        }
    }

    // Returns by value for scalars / strings / vectors / enums, but by
    // REFERENCE for wrapped class types: the C++ object lives inside the JS
    // object's Wrap, so handing out `T&` (a) lets a bound function mutate the
    // caller-visible object through `T&` / out-parameters, and (b) avoids
    // copying types whose copy is shallow or deleted (e.g. pImpl facades —
    // a by-value return would copy the pointer and dangle it when the
    // temporary is destroyed).
    template <typename T> decltype(auto) from_napi(const Napi::Value &v) {
        if constexpr (std::is_same_v<T, std::string>) {
            return v.As<Napi::String>().Utf8Value();
        } else if constexpr (std::is_same_v<T, bool>) {
            return v.As<Napi::Boolean>().Value();
        } else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(v.As<Napi::Number>().DoubleValue());
        } else if constexpr (std::is_integral_v<T>) {
            return static_cast<T>(v.As<Napi::Number>().Int64Value());
        } else if constexpr (is_std_vector<T>::value) {
            using Elem      = typename T::value_type;
            Napi::Array arr = v.As<Napi::Array>();
            T           out;
            out.reserve(arr.Length());
            for (uint32_t i = 0; i < arr.Length(); ++i) {
                out.push_back(from_napi<Elem>(arr.Get(i)));
            }
            return out;
        } else if constexpr (std::is_enum_v<T>) {
            return static_cast<T>(
                static_cast<std::underlying_type_t<T>>(v.As<Napi::Number>().Int64Value()));
        } else if constexpr (std::is_class_v<T>) {
            return static_cast<T &>(Wrap<T>::Unwrap(v.As<Napi::Object>())->inner);
        } else {
            static_assert(sizeof(T) == 0, "from_napi: unsupported type");
        }
    }

    template <typename T> inline bool napi_is_overridden(Napi::Object self, const char *name) {
        Napi::Value f = self.Get(name);
        if (!f.IsFunction()) {
            return false;
        }
        auto &guard = napi_override_guard<T>();
        auto  it    = guard.find(name);
        if (it == guard.end()) {
            return false;
        }
        return !f.StrictEquals(it->second.Value());
    }

    template <typename T, typename Ret, typename Base, typename... Args>
    inline Ret napi_call_override(const NapiTrampoline &self, const char *name, Base base,
                                  const Args &...args) {
        if (self.__rosetta_has_self()) {
            Napi::Object obj = self.__rosetta_self();
            if (napi_is_overridden<T>(obj, name)) {
                Napi::Value r = obj.Get(name).template As<Napi::Function>().Call(
                    obj, {to_napi(obj.Env(), args)...});
                if constexpr (std::is_void_v<Ret>) {
                    return;
                } else {
                    return from_napi<Ret>(r);
                }
            }
        }
        return base();
    }

    template <typename T, typename Ret, typename... Args>
    inline Ret napi_call_override_pure(const NapiTrampoline &self, const char *name,
                                       const Args &...args) {
        if (self.__rosetta_has_self()) {
            Napi::Object obj = self.__rosetta_self();
            if (napi_is_overridden<T>(obj, name)) {
                Napi::Value r = obj.Get(name).template As<Napi::Function>().Call(
                    obj, {to_napi(obj.Env(), args)...});
                if constexpr (std::is_void_v<Ret>) {
                    return;
                } else {
                    return from_napi<Ret>(r);
                }
            }
            throw Napi::Error::New(obj.Env(), std::string("rosetta: pure virtual '") + name +
                                                  "' is not overridden in JS");
        }
        throw std::runtime_error(std::string("rosetta: pure virtual '") + name +
                                 "' called before the JS object was bound");
    }

    // ---- CRTP wrapper: accessors keyed on member/function pointers ----

    template <typename T, typename Tramp> class Wrap : public Napi::ObjectWrap<Wrap<T, Tramp>> {
      public:
        Tramp inner;

        Wrap(const Napi::CallbackInfo &info) : Napi::ObjectWrap<Wrap<T, Tramp>>(info) {
            if constexpr (!std::is_same_v<T, Tramp>) {
                inner.__rosetta_set_self(this->Value());
            }
            // The parameterized-constructor path ASSIGNS the freshly built
            // object into `inner`. For a non-assignable class (GEO::Mesh) the
            // statement itself would not compile — the emitter registers no
            // ctor_table entries for such a class, so compile the whole path
            // out and keep only the default constructor.
            if constexpr (std::is_copy_assignable_v<T> || std::is_move_assignable_v<T>) {
                auto &tbl = ctor_table<Tramp>();
                auto  it  = tbl.find(info.Length());
                if (it != tbl.end()) {
                    static_cast<T &>(inner) = it->second(info);
                    return;
                }
            }
            if (info.Length() > 0) {
                throw Napi::TypeError::New(info.Env(), "no matching constructor for " +
                                                           std::to_string(info.Length()) +
                                                           " argument(s)");
            }
        }

        template <auto MemPtr> Napi::Value get_field(const Napi::CallbackInfo &info) {
            return to_napi(info.Env(), inner.*MemPtr);
        }

        template <auto MemPtr>
        void set_field(const Napi::CallbackInfo & /*info*/, const Napi::Value &v) {
            using FieldT  = std::remove_cvref_t<decltype(std::declval<T &>().*MemPtr)>;
            inner.*MemPtr = from_napi<FieldT>(v);
        }

        template <auto MemPtr, fixed_str Name, double Lo, double Hi>
        void set_field_ranged(const Napi::CallbackInfo &info, const Napi::Value &v) {
            using FieldT = std::remove_cvref_t<decltype(std::declval<T &>().*MemPtr)>;
            FieldT val   = from_napi<FieldT>(v);
            double d     = static_cast<double>(val);
            if (d < Lo || d > Hi) {
                throw Napi::RangeError::New(info.Env(),
                                            std::string(Name.data) + " out of range");
            }
            inner.*MemPtr = val;
        }

        template <auto /*MemPtr*/, fixed_str Name>
        void set_field_readonly(const Napi::CallbackInfo &info, const Napi::Value & /*v*/) {
            throw Napi::TypeError::New(info.Env(), std::string(Name.data) + " is read-only");
        }

        template <auto MFP> Napi::Value call_method(const Napi::CallbackInfo &info) {
            return call_method_impl<MFP>(info,
                                         std::make_index_sequence<fn_traits<decltype(MFP)>::arity>{});
        }

        // Extension method: a FREE function whose first parameter is `T&` (or
        // `const T&`), exposed as an instance method — the wrapped object is
        // passed as the receiver and info[i] maps to parameter i+1.
        template <auto FP> Napi::Value ext_method(const Napi::CallbackInfo &info) {
            return ext_method_impl<FP>(
                info, std::make_index_sequence<fn_traits<decltype(FP)>::arity - 1>{});
        }

        template <auto FP> static Napi::Value call_static(const Napi::CallbackInfo &info) {
            return call_static_impl<FP>(info,
                                        std::make_index_sequence<fn_traits<decltype(FP)>::arity>{});
        }

      private:
        template <auto FP, std::size_t... Is>
        Napi::Value ext_method_impl(const Napi::CallbackInfo &info, std::index_sequence<Is...>) {
            using FT = fn_traits<decltype(FP)>;
            using R  = typename FT::ret;
            T &self  = static_cast<T &>(inner);
            if constexpr (std::is_void_v<R>) {
                (*FP)(self, from_napi<std::remove_cvref_t<typename FT::template arg<Is + 1>>>(
                                info[Is])...);
                return info.Env().Undefined();
            } else {
                R r = (*FP)(self, from_napi<std::remove_cvref_t<typename FT::template arg<Is + 1>>>(
                                      info[Is])...);
                return to_napi(info.Env(), r);
            }
        }

        template <auto MFP, std::size_t... Is>
        Napi::Value call_method_impl(const Napi::CallbackInfo &info, std::index_sequence<Is...>) {
            using FT = fn_traits<decltype(MFP)>;
            using R  = typename FT::ret;
            if constexpr (std::is_void_v<R>) {
                (inner.*MFP)(
                    from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
                return info.Env().Undefined();
            } else {
                R r = (inner.*MFP)(
                    from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
                return to_napi(info.Env(), r);
            }
        }

        template <auto FP, std::size_t... Is>
        static Napi::Value call_static_impl(const Napi::CallbackInfo &info,
                                            std::index_sequence<Is...>) {
            using FT = fn_traits<decltype(FP)>;
            using R  = typename FT::ret;
            if constexpr (std::is_void_v<R>) {
                (*FP)(from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
                return info.Env().Undefined();
            } else {
                R r = (*FP)(
                    from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
                return to_napi(info.Env(), r);
            }
        }
    };

    template <typename T> Napi::FunctionReference &ctor_ref() {
        static Napi::FunctionReference ref;
        return ref;
    }

    // ---- Free-function entry, keyed on the function pointer ----

    template <auto FP, std::size_t... Is>
    inline Napi::Value napi_free_call(const Napi::CallbackInfo &info, std::index_sequence<Is...>) {
        using FT = fn_traits<decltype(FP)>;
        using R  = typename FT::ret;
        if constexpr (std::is_void_v<R>) {
            (*FP)(from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
            return info.Env().Undefined();
        } else {
            R r =
                (*FP)(from_napi<std::remove_cvref_t<typename FT::template arg<Is>>>(info[Is])...);
            return to_napi(info.Env(), r);
        }
    }

    template <auto FP> inline Napi::Value napi_free_entry(const Napi::CallbackInfo &info) {
        return napi_free_call<FP>(info, std::make_index_sequence<fn_traits<decltype(FP)>::arity>{});
    }

    // ---- Enum object from an explicit name/value list (no reflection) ----

    inline Napi::Object
    make_enum(Napi::Env env,
              std::initializer_list<std::pair<const char *, long long>> values) {
        Napi::Object obj = Napi::Object::New(env);
        for (const auto &[name, value] : values) {
            obj.Set(name, Napi::Number::New(env, static_cast<double>(value)));
        }
        obj.Freeze();
        return obj;
    }

} // namespace rosetta
