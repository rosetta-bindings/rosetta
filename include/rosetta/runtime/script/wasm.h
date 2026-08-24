// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED
//
// embind type caster for rosetta::script::Value — target `wasm`.
// See <rosetta/runtime/script_casters.h> for what this is and how it gets in.
//
// OPT-IN. Turn it on with one manifest line:
//
//     "compile_definitions": ["ROSETTA_EMBIND_VALUE_CASTER"]
//
// Off by default because it is the one caster that must also DISABLE part of
// the generated code (see class_ below) — a bigger claim on the module than the
// other three make, and reversible with one line if it ever collides with a
// future backend change. Exercised by examples/scriptable-model/drive_web.html.
//
// Three things embind wants that the other casters' frameworks do not, each of
// which fails in its own way if you skip it — all three commented at the point
// they apply: the rvp tag on toWireType, a TypeID that agrees with it, and
// release_ownership() rather than as_handle().

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

            // The return-value-policy tag is not optional: embind calls
            // toWireType(v, rvp::default_tag{}) from every emitted thunk, so a
            // one-argument overload compiles here and fails at every use site.
            static WireType toWireType(const rosetta::script::Value &v, rvp::default_tag) {
                // release_ownership(), not as_handle(): the wire type IS the
                // reference the caller inherits, and as_handle() leaves the
                // temporary val holding it — decref'd on the way out of this
                // function, so JS reads a freed slot ("invalid handle: N").
                return rosetta::script::visit(v, rosetta::script::casters::val_sink{})
                    .release_ownership();
            }

            static rosetta::script::Value fromWireType(WireType w) {
                return rosetta::script::casters::from_val(val::take_ownership(w));
            }
        };

        // BindingType is only half of it. The wire format above is what C++
        // WRITES; which JS-side converter reads it is chosen by the TYPEID
        // embedded in the registered signature, and `class_<Value>` claims
        // LightTypeID<Value> — "a pointer to a Value, wrap it in the Value
        // handle class". Leave that in place and every Value-returning function
        // hands JS an EM_VAL reinterpreted as a Value*: a live handle onto a
        // garbage object, which fails at the first method call, not at the
        // boundary. Point the id at val's instead, so the two halves agree.
        //
        // TypeID<const T> / TypeID<T&> / TypeID<T&&> forward to TypeID<T>, so
        // this one specialization covers `Instance::set(..., const Value &)`
        // and every other spelling in the API.
        template <> struct TypeID<rosetta::script::Value> {
            static constexpr TYPEID get() { return TypeID<val>::get(); }
        };

    } // namespace internal

    // ...and now the generated `class_<Value>("Value")` has to go, because it
    // registers a SECOND converter under the id above — "Cannot register type
    // 'Value' twice", thrown while the module loads, before any of it runs.
    //
    // The generator cannot know a caster exists (that is the whole premise of
    // script_casters.h), and no manifest field drops one class from one target
    // while keeping it in the type gate — which Value must stay in, or every
    // member mentioning it is skipped. So the caster retracts the registration
    // itself: class_<Value> becomes a chainable no-op, and the emitted
    // .constructor<>() / .function(...) / .class_function(...) chain compiles
    // to nothing. The handle class disappears from the module — which is the
    // point. A Value is a JS value now; there is nothing left to wrap.
    template <> class class_<rosetta::script::Value, internal::NoBaseClass> {
      public:
        explicit class_(const char *, ...) {}

        template <typename... Args> const class_ &constructor(Args &&...) const { return *this; }
        template <typename... Args> const class_ &function(const char *, Args &&...) const {
            return *this;
        }
        template <typename... Args> const class_ &class_function(const char *, Args &&...) const {
            return *this;
        }
        template <typename... Args> const class_ &property(const char *, Args &&...) const {
            return *this;
        }
    };

} // namespace emscripten

#endif // embind
