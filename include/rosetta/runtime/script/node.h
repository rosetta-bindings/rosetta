// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED
//
// N-API type caster for rosetta::script::Value — target `node`.
// See <rosetta/runtime/script_casters.h> for what this is and how it gets in.
//
// rosetta's node runtime converts through two function templates rather than a
// trait, so the caster is an explicit specialization of each.

#pragma once

#include "../../script.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#if defined(NAPI_VERSION)

#include "../node.h" // rosetta::to_napi / from_napi / ctor_ref

namespace rosetta::script::casters {

    /** @brief Value -> JS: the sink half of visit(). */
    struct napi_sink {
        Napi::Env env;

        Napi::Value on_none() { return env.Null(); }
        Napi::Value on_bool(bool b) { return Napi::Boolean::New(env, b); }
        Napi::Value on_int(long long i) { return Napi::Number::New(env, static_cast<double>(i)); }
        Napi::Value on_real(double d) { return Napi::Number::New(env, d); }
        Napi::Value on_string(const std::string &s) { return Napi::String::New(env, s); }
        Napi::Value on_enum(long long i, const TypeInfo &) { return on_int(i); }

        Napi::Value on_list(const std::vector<Value> &xs) {
            Napi::Array arr = Napi::Array::New(env, xs.size());
            for (std::size_t i = 0; i < xs.size(); ++i) {
                arr.Set(static_cast<std::uint32_t>(i), visit(xs[i], *this));
            }
            return arr;
        }

        Napi::Value on_object(const Instance &o) {
            return rosetta::to_napi<rosetta::script::Instance>(env, o);
        }
        Napi::Value on_unknown(const std::string &s) { return on_string(s); }
    };

} // namespace rosetta::script::casters

namespace rosetta {

    template <>
    inline Napi::Value to_napi<rosetta::script::Value>(Napi::Env                     env,
                                                       const rosetta::script::Value &v) {
        return rosetta::script::visit(v, rosetta::script::casters::napi_sink{env});
    }

    /** @brief JS -> Value: the irreducibly per-language half. */
    template <> inline decltype(auto) from_napi<rosetta::script::Value>(const Napi::Value &v) {
        using V = rosetta::script::Value;
        if (v.IsNull() || v.IsUndefined()) {
            return V::none();
        }
        if (v.IsBoolean()) {
            return V::boolean(v.As<Napi::Boolean>().Value());
        }
        if (v.IsNumber()) {
            // JS has one number type; rosetta's overload scoring does not. A
            // value that is exactly integral is boxed as one, so `at(2)` scores
            // `exact` against `at(int)` instead of `promote`.
            const double d = v.As<Napi::Number>().DoubleValue();
            if (std::isfinite(d) && std::trunc(d) == d && d >= -9.2e18 && d <= 9.2e18) {
                return V::integer(static_cast<long long>(d));
            }
            return V::number(d);
        }
        if (v.IsString()) {
            return V::text(v.As<Napi::String>().Utf8Value());
        }
        if (v.IsArray()) {
            Napi::Array    arr = v.As<Napi::Array>();
            std::vector<V> items;
            items.reserve(arr.Length());
            for (std::uint32_t i = 0; i < arr.Length(); ++i) {
                items.push_back(from_napi<V>(arr.Get(i)));
            }
            return V::list(items);
        }
        if (v.IsObject()) {
            Napi::Object obj = v.As<Napi::Object>();
            if (!ctor_ref<rosetta::script::Instance>().IsEmpty() &&
                obj.InstanceOf(ctor_ref<rosetta::script::Instance>().Value())) {
                return V::from(from_napi<rosetta::script::Instance>(v));
            }
        }
        return V::none();
    }

} // namespace rosetta

#endif // N-API
