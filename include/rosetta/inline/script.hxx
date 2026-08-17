// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED
//
// Bodies for <rosetta/script.h>. Included from it; never on its own.

#pragma once

#include "../script.h"

namespace rosetta::script {

    // Inline bodies
    // =====================================================================

    namespace detail {

        struct Access {
            /** @brief Wrap one metadata pointer as its handle. */
            template <class Handle, class Ptr> static Handle make(const Ptr *p) {
                return Handle(p);
            }
            static Value    value(dyn::Any a) { return Value(std::move(a)); }
            static Outcome  outcome(Value v, std::string e) {
                return Outcome(std::move(v), std::move(e));
            }
            static Instance instance(dyn::Object o) { return Instance(std::move(o)); }
        };

        inline dyn::ArgList to_args(const std::vector<Value> &vs) {
            dyn::ArgList a;
            for (const Value &v : vs)
                a.add(v.raw());
            return a;
        }

        inline Outcome outcome(const dyn::Result &r) {
            return r.ok() ? Access::outcome(Access::value(r.value), std::string())
                          : Access::outcome(Value(), r.error);
        }

        template <class Handle, class Ptr>
        inline std::vector<Handle> span(const Ptr *p, std::size_t n) {
            std::vector<Handle> out;
            out.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                out.push_back(Access::make<Handle>(p + i));
            return out;
        }

        inline std::vector<Annotation> annots(const dyn::MetaAnnotation *a, std::size_t n) {
            std::vector<Annotation> out;
            out.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                out.emplace_back(a[i].key, a[i].value);
            return out;
        }

    } // namespace detail

    // ---- Value ----------------------------------------------------------

    inline Value Value::list(const std::vector<Value> &vs) {
        std::vector<dyn::Any> raw;
        raw.reserve(vs.size());
        for (const Value &v : vs)
            raw.push_back(v.raw());
        // No descriptor to pass: a host-language sequence has no single C++
        // element type. Any::list substitutes a Kind::vector builtin.
        return Value(dyn::Any::list(std::move(raw)));
    }

    inline Value Value::from(const Instance &o) { return o.as_value(); }

    inline TypeInfo Value::type() const { return TypeInfo(any_.type()); }

    inline std::vector<Value> Value::as_list() const {
        std::vector<Value> out;
        for (const dyn::Any &a : any_.as_list())
            out.push_back(Value(a));
        return out;
    }

    inline Instance Value::as_object() const {
        const dyn::ObjectRef &r = any_.as_object();
        return Instance(r.owner ? dyn::Object::adopt(*r.cls, r.ptr, r.owner)
                                : dyn::Object::borrow(*r.cls, r.ptr));
    }

    // ---- TypeInfo -------------------------------------------------------

    inline ClassInfo TypeInfo::cls() const { return ClassInfo(td_ ? td_->cls : nullptr); }

    inline std::vector<std::string> TypeInfo::enumerator_names() const {
        std::vector<std::string> out;
        if (td_)
            for (std::size_t i = 0; i < td_->n_enumerators; ++i)
                out.emplace_back(td_->enumerators[i].name);
        return out;
    }

    inline std::vector<long long> TypeInfo::enumerator_values() const {
        std::vector<long long> out;
        if (td_)
            for (std::size_t i = 0; i < td_->n_enumerators; ++i)
                out.push_back(td_->enumerators[i].value);
        return out;
    }

    // ---- EnumInfo -------------------------------------------------------

    inline std::vector<std::string> EnumInfo::names() const {
        std::vector<std::string> out;
        if (e_)
            for (std::size_t i = 0; i < e_->n_values; ++i)
                out.emplace_back(e_->values[i].name);
        return out;
    }

    inline std::vector<long long> EnumInfo::values() const {
        std::vector<long long> out;
        if (e_)
            for (std::size_t i = 0; i < e_->n_values; ++i)
                out.push_back(e_->values[i].value);
        return out;
    }

    // ---- FieldInfo ------------------------------------------------------

    inline std::vector<std::string> FieldInfo::choices() const {
        std::vector<std::string> out;
        if (f_)
            for (std::size_t i = 0; i < f_->n_choices; ++i)
                out.emplace_back(f_->choices[i]);
        return out;
    }

    inline std::vector<Annotation> FieldInfo::annotations() const {
        return f_ ? detail::annots(f_->annotations, f_->n_annotations) : std::vector<Annotation>{};
    }

    // ---- MethodInfo / CtorInfo / FunctionInfo ---------------------------

    inline std::vector<ParamInfo> MethodInfo::params() const {
        return m_ ? detail::span<ParamInfo>(m_->params, m_->n_params) : std::vector<ParamInfo>{};
    }

    inline std::vector<Annotation> MethodInfo::annotations() const {
        return m_ ? detail::annots(m_->annotations, m_->n_annotations) : std::vector<Annotation>{};
    }

    inline std::vector<ParamInfo> CtorInfo::params() const {
        return c_ ? detail::span<ParamInfo>(c_->params, c_->n_params) : std::vector<ParamInfo>{};
    }

    inline std::vector<ParamInfo> FunctionInfo::params() const {
        return f_ ? detail::span<ParamInfo>(f_->params, f_->n_params) : std::vector<ParamInfo>{};
    }

    inline Outcome FunctionInfo::call(const std::vector<Value> &args) const {
        if (!f_)
            return Outcome(Value(), "no such function");
        return detail::outcome(dyn::call_function(*f_, detail::to_args(args)));
    }

    // ---- ClassInfo ------------------------------------------------------

    inline std::vector<ClassInfo> ClassInfo::bases() const {
        std::vector<ClassInfo> out;
        if (k_)
            for (std::size_t i = 0; i < k_->n_bases; ++i)
                out.push_back(ClassInfo(k_->bases[i])); // array OF pointers, not a span
        return out;
    }

    inline std::vector<FieldInfo> ClassInfo::fields() const {
        return k_ ? detail::span<FieldInfo>(k_->fields, k_->n_fields) : std::vector<FieldInfo>{};
    }

    inline std::vector<MethodInfo> ClassInfo::methods() const {
        return k_ ? detail::span<MethodInfo>(k_->methods, k_->n_methods) : std::vector<MethodInfo>{};
    }

    inline std::vector<CtorInfo> ClassInfo::ctors() const {
        return k_ ? detail::span<CtorInfo>(k_->ctors, k_->n_ctors) : std::vector<CtorInfo>{};
    }

    inline std::vector<Annotation> ClassInfo::annotations() const {
        return k_ ? detail::annots(k_->annotations, k_->n_annotations) : std::vector<Annotation>{};
    }

    inline FieldInfo ClassInfo::field(const std::string &n) const {
        return FieldInfo(k_ ? k_->field(n) : nullptr);
    }

    inline MethodInfo ClassInfo::method(const std::string &n) const {
        return MethodInfo(k_ ? k_->method(n) : nullptr);
    }

    inline std::vector<MethodInfo> ClassInfo::overloads(const std::string &n) const {
        std::vector<MethodInfo> out;
        if (k_)
            for (const dyn::MetaMethod *m : k_->overloads(n))
                out.push_back(MethodInfo(m));
        return out;
    }

    inline bool ClassInfo::derives_from(const ClassInfo &b) const {
        return k_ && b.k_ && k_->derives_from(b.k_);
    }

    inline Outcome ClassInfo::create(const std::vector<Value> &args) const {
        if (!k_)
            return Outcome(Value(), "no such class");
        dyn::Result r = dyn::Object::create(*k_, detail::to_args(args));
        return detail::outcome(r);
    }

    inline Outcome ClassInfo::call_static(const std::string &name,
                                          const std::vector<Value> &args) const {
        if (!k_)
            return Outcome(Value(), "no such class");
        return detail::outcome(dyn::call_static(*k_, name, detail::to_args(args)));
    }

    inline std::string ClassInfo::why_no_match(const std::string             &name,
                                               const std::vector<Value> &args) const {
        if (!k_)
            return "no such class";
        std::string why;
        return dyn::resolve(*k_, name, detail::to_args(args), &why) ? std::string() : why;
    }

    // ---- Instance -------------------------------------------------------

    inline Outcome Instance::get(const std::string &field) const {
        return detail::outcome(obj_.get(field));
    }

    inline Outcome Instance::set(const std::string &field, const Value &v) const {
        return detail::outcome(obj_.set(field, v.raw()));
    }

    inline Outcome Instance::call(const std::string &method, const std::vector<Value> &args) const {
        return detail::outcome(obj_.call(method, detail::to_args(args)));
    }

    inline std::string Instance::to_string() const {
        if (!valid())
            return "<invalid>";
        return std::string("<") + obj_.meta()->qualified + ">";
    }

    // ---- registry -------------------------------------------------------

    inline std::vector<ClassInfo> classes() {
        std::vector<ClassInfo> out;
        for (const dyn::MetaClass *k : dyn::registry().classes())
            out.push_back(ClassInfo(k));
        return out;
    }

    inline std::vector<EnumInfo> enums() {
        std::vector<EnumInfo> out;
        for (const dyn::MetaEnum *e : dyn::registry().enums())
            out.push_back(EnumInfo(e));
        return out;
    }

    inline std::vector<FunctionInfo> functions() {
        std::vector<FunctionInfo> out;
        for (const dyn::MetaFunction *f : dyn::registry().functions())
            out.push_back(FunctionInfo(f));
        return out;
    }

    inline ClassInfo find_class(const std::string &name) {
        return ClassInfo(dyn::registry().find_class(name));
    }

    inline EnumInfo find_enum(const std::string &name) {
        return EnumInfo(dyn::registry().find_enum(name));
    }

    inline FunctionInfo find_function(const std::string &name) {
        return FunctionInfo(dyn::registry().find_function(name));
    }

    inline std::vector<FunctionInfo> function_overloads(const std::string &name) {
        std::vector<FunctionInfo> out;
        for (const dyn::MetaFunction *f : dyn::registry().function_overloads(name))
            out.push_back(FunctionInfo(f));
        return out;
    }

    inline Outcome create(const std::string &class_name, const std::vector<Value> &args) {
        return find_class(class_name).create(args);
    }

    // ---- visit ----------------------------------------------------------

    template <class Sink> decltype(auto) visit(const Value &v, Sink &&sink) {
        // An empty box is `none` whatever its descriptor claims — a
        // default-constructed Value carries no TypeDesc at all.
        if (v.empty()) {
            return sink.on_none();
        }
        switch (v.raw().kind()) {
        case dyn::Kind::void_:
            return sink.on_none();
        case dyn::Kind::boolean:
            return sink.on_bool(v.as_bool());
        case dyn::Kind::number:
            // The integral/floating split lives in the TypeDesc, not in the
            // box: Any::integer and Any::real both canonicalise, and only the
            // descriptor remembers which the C++ side said.
            return v.type().is_integral() ? sink.on_int(v.as_int()) : sink.on_real(v.as_number());
        case dyn::Kind::string:
            return sink.on_string(v.as_string());
        case dyn::Kind::enum_:
            return sink.on_enum(v.as_int(), v.type());
        case dyn::Kind::vector:
            return sink.on_list(v.as_list());
        case dyn::Kind::object:
            return sink.on_object(v.as_object());
        case dyn::Kind::unknown:
            break;
        }
        return sink.on_unknown(v.to_string());
    }

} // namespace rosetta::script
