// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED
//
// embind type caster for rosetta::script::Value — target `wasm`.
// See <rosetta/runtime/script_casters.h> for what this is and how it gets in.
//
// UNTESTED, AND OPT-IN. Written against embind's BindingType customization
// point but never compiled: there was no emsdk on the machine this was
// developed on, and shipping a block that breaks the wasm build by default is
// worse than shipping none. Turn it on with a manifest line once you have emcc:
//
//     "compile_definitions": ["ROSETTA_EMBIND_VALUE_CASTER"]
//
// and treat the first build as the review.

#pragma once

#include "../../script.h"

#include <cmath>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__) && defined(ROSETTA_EMBIND_VALUE_CASTER)

#include <emscripten/bind.h>
#include <emscripten/val.h>

namespace rosetta::script::casters {

    /** @brief Value -> JS: the sink half of visit(). */
    struct val_sink {
        emscripten::val on_none() { return emscripten::val::null(); }
        emscripten::val on_bool(bool b) { return emscripten::val(b); }
        emscripten::val on_int(long long i) { return emscripten::val(static_cast<double>(i)); }
        emscripten::val on_real(double d) { return emscripten::val(d); }
        emscripten::val on_string(const std::string &s) { return emscripten::val(s); }
        emscripten::val on_enum(long long i, const TypeInfo &) { return on_int(i); }

        emscripten::val on_list(const std::vector<Value> &xs) {
            emscripten::val out = emscripten::val::array();
            int             i   = 0;
            for (const Value &e : xs) {
                out.set(i++, visit(e, *this));
            }
            return out;
        }

        emscripten::val on_object(const Instance &o) { return emscripten::val(o); }
        emscripten::val on_unknown(const std::string &s) { return on_string(s); }
    };

    /** @brief JS -> Value: the irreducibly per-language half. */
    inline Value from_val(const emscripten::val &j) {
        const std::string type = j.typeOf().as<std::string>();
        if (j.isNull() || j.isUndefined()) {
            return Value::none();
        }
        if (type == "boolean") {
            return Value::boolean(j.as<bool>());
        }
        if (type == "number") {
            const double d = j.as<double>();
            return (std::trunc(d) == d) ? Value::integer(static_cast<long long>(d))
                                        : Value::number(d);
        }
        if (type == "string") {
            return Value::text(j.as<std::string>());
        }
        if (j.isArray()) {
            std::vector<Value> items;
            const unsigned     n = j["length"].as<unsigned>();
            for (unsigned i = 0; i < n; ++i) {
                items.push_back(from_val(j[i]));
            }
            return Value::list(items);
        }
        if (j.instanceof (emscripten::val::module_property("Instance"))) {
            return Value::from(j.as<Instance>());
        }
        return Value::none();
    }

} // namespace rosetta::script::casters

namespace emscripten {
    namespace internal {

        template <> struct BindingType<rosetta::script::Value> {
            using WireType = EM_VAL;

            static WireType toWireType(const rosetta::script::Value &v) {
                return rosetta::script::visit(v, rosetta::script::casters::val_sink{}).as_handle();
            }

            static rosetta::script::Value fromWireType(WireType w) {
                return rosetta::script::casters::from_val(val::take_ownership(w));
            }
        };

    } // namespace internal
} // namespace emscripten

#endif // embind
