// Copyright (c) fmaerten@gmail.com
// License: MIT
//
// A BINDABLE facade over <rosetta/dynamic.h> — the reflection API reshaped so
// that rosetta's own backends can bind it.
//
// WHY THIS FILE EXISTS
//
// rosetta::dyn is complete and it is C++-only. Every consumer of it today
// (examples/dynamic/interp.h, examples/dynamic/qt/*, runtime/imgui.h,
// visitors/qml_reflected_object.h) is hand-written C++ walking the metadata.
// The Graphite/GOM move is to wrap the metadata API ITSELF, so the walker can
// be written in Python / Lua / JS instead — one UI generator per toolkit
// written once in a scripting language, rather than one C++ backend per
// toolkit.
//
// dyn cannot be fed to the backends as-is. Three shapes block it:
//
//   1. pointer-and-count spans      (MetaField *fields; size_t n_fields)
//      -> the vector marshalling never fires; scripts would need pointer math.
//   2. raw const T * returns        (const MetaClass *, const TypeDesc *)
//      -> no ownership story, and the type gate skips them.
//   3. function-pointer thunks      (using Thunk = Any (*)(...))
//      -> not marshalable in any target.
//
// So: value-semantics handles holding a `const dyn::X *`, exposing spans as
// std::vector<Handle> and thunks as ordinary methods. No copies of the
// metadata are made — every handle is one pointer, the tables stay in .rodata.
//
// NAMING. Nothing here is called Class / Type / Object / Method on purpose:
// those names collide in the target languages (java.lang.Class, JS Object,
// Lua's `type`). ClassInfo / TypeInfo / Instance / MethodInfo survive every
// backend's flat namespace.
//
// ERRORS. Calls return Outcome (ok + error text), never an exception, for the
// reason dyn::Result states: unwinding through a foreign VM's frames is how
// you get a crash instead of a TypeError. A per-language shim can turn a
// failed Outcome into a native exception at the boundary.
//
// HOW TO USE IT. Point a manifest at this header, list the handle classes and
// the registry free functions, and publish your library's `dynamic` tables from
// module_init — see examples/scriptable-model/manifest.json, which is the whole
// of the per-project work. Pair it with <rosetta/runtime/script_casters.h> so
// Value crosses as a native float / string / list rather than as a box.

#pragma once

#include "dynamic.h"

#include <string>
#include <utility>
#include <vector>

namespace rosetta::script {

    class TypeInfo;
    class FieldInfo;
    class MethodInfo;
    class ParamInfo;
    class CtorInfo;
    class ClassInfo;
    class EnumInfo;
    class FunctionInfo;
    class Value;
    class Instance;

    /** @brief The one key to every handle's private constructor.
     *
     * The handles take a `const dyn::X *` that no target language can spell, so
     * those constructors stay private — otherwise each backend would have to
     * decide what to do with a constructor whose parameter it cannot marshal.
     * The span/outcome helpers below need them anyway; Access is how they get
     * in, and it is the whole of the trust boundary. */
    namespace detail {
        struct Access;
    }

    // =====================================================================
    // Value — the one type that wants a hand-written caster per language
    // =====================================================================

    /**
     * @brief A dyn::Any behind an interface every backend can already bind.
     *
     * Bound as-is this is usable but not idiomatic: a script writes
     * `v.as_number()` where it wants a plain float. That last mile is a
     * per-language type caster (pybind11 type_caster<Value>, an N-API
     * converter, sol2 push/get) — see casters/ for the pybind11 one. The
     * caster is pure polish: everything works without it.
     */
    class Value {
    public:
        Value() = default;

        // ---- construction from the host language -----------------------
        static Value none() { return Value(dyn::Any::none()); }
        static Value boolean(bool v) { return Value(dyn::Any::boolean(v)); }
        static Value integer(long long v) { return Value(dyn::Any::integer(v)); }
        static Value number(double v) { return Value(dyn::Any::real(v)); }
        static Value text(const std::string &v) { return Value(dyn::Any::text(v)); }
        static Value list(const std::vector<Value> &vs);
        static Value from(const Instance &o);

        // ---- inspection ------------------------------------------------
        /** @brief "void" | "boolean" | "number" | "string" | "enum" | "object"
         *         | "vector" | "unknown" — dyn::kind_name, as a string because
         *         an enum crossing the boundary buys nothing here. */
        std::string kind() const { return dyn::kind_name(any_.kind()); }
        bool        empty() const { return any_.empty(); }
        TypeInfo    type() const;

        // ---- canonical accessors --------------------------------------
        // Each mirrors dyn::Any's contract. They throw dyn::Error on a kind
        // mismatch, which is the one place an exception is acceptable: it is a
        // script bug (asked a string for a number), not a call failure.
        bool               as_bool() const { return any_.as_bool(); }
        long long          as_int() const { return any_.as_int(); }
        double             as_number() const { return any_.as_number(); }
        const std::string &as_string() const { return any_.as_string(); }
        std::vector<Value> as_list() const;
        Instance           as_object() const;

        /** @brief Human-readable rendering — REPLs, tooltips, error text. */
        std::string to_string() const { return any_.to_string(); }

        const dyn::Any &raw() const { return any_; }

    private:
        explicit Value(dyn::Any a) : any_(std::move(a)) {}

        dyn::Any any_;

        friend struct detail::Access;
        friend class Instance;
        friend class FieldInfo;
        friend class MethodInfo;
        friend class FunctionInfo;
        friend class ClassInfo;
    };

    /** @brief A call's result: a value, or the reason there isn't one. */
    class Outcome {
    public:
        Outcome() = default;

        bool               ok() const { return error_.empty(); }
        const std::string &error() const { return error_; }
        Value              value() const { return value_; }

    private:
        Outcome(Value v, std::string e) : value_(std::move(v)), error_(std::move(e)) {}

        Value       value_;
        std::string error_;

        friend struct detail::Access;
        friend class Instance;
        friend class ClassInfo;
        friend class FunctionInfo;
    };

    // =====================================================================
    // Type
    // =====================================================================

    /** @brief What a value IS — the bindable face of dyn::TypeDesc. */
    class TypeInfo {
    public:
        TypeInfo() = default;

        bool valid() const { return td_ != nullptr; }

        std::string kind() const { return td_ ? dyn::kind_name(td_->kind) : "unknown"; }
        /** @brief Prettified C++ spelling — for tooltips and error text. */
        std::string spelling() const { return td_ ? td_->spelling : ""; }
        /** @brief Qualified class / enum name when kind is object or enum. */
        std::string object_name() const { return td_ ? td_->object : ""; }

        bool is_integral() const { return td_ && td_->integral; }
        bool is_pointer() const { return td_ && td_->is_pointer; }
        bool is_shared_ptr() const { return td_ && td_->is_shared_ptr; }

        /** @brief Vector element / shared_ptr pointee; invalid when neither.
         *  This is why TypeInfo is not a type name: a UI needs "what is inside
         *  it", and only a structural descriptor answers that. */
        TypeInfo element() const { return TypeInfo(td_ ? td_->element : nullptr); }

        /** @brief The bound class this type names, when kind == object. */
        ClassInfo cls() const;

        /** @brief Enumerator names, in declaration order (kind == enum). */
        std::vector<std::string> enumerator_names() const;
        /** @brief Enumerator values, parallel to enumerator_names(). */
        std::vector<long long> enumerator_values() const;

    private:
        explicit TypeInfo(const dyn::TypeDesc *t) : td_(t) {}
        const dyn::TypeDesc *td_ = nullptr;

        friend struct detail::Access;
        friend class Value;
        friend class FieldInfo;
        friend class MethodInfo;
        friend class ParamInfo;
        friend class ClassInfo;
        friend class FunctionInfo;
    };

    // =====================================================================
    // Members
    // =====================================================================

    /** @brief One free-form annotation the core does not interpret. */
    class Annotation {
    public:
        Annotation() = default;
        Annotation(std::string k, std::string v) : key_(std::move(k)), value_(std::move(v)) {}

        const std::string &key() const { return key_; }
        /** @brief JSON scalar or object, as text. */
        const std::string &value() const { return value_; }

    private:
        std::string key_, value_;
    };

    /** @brief A field: everything a property editor needs, in one object. */
    class FieldInfo {
    public:
        FieldInfo() = default;

        bool        valid() const { return f_ != nullptr; }
        std::string name() const { return f_ ? f_->name : ""; }
        std::string doc() const { return f_ ? f_->doc : ""; }
        TypeInfo    type() const { return TypeInfo(f_ ? f_->type : nullptr); }

        // ---- UI hints, the reason this whole exercise pays -------------
        bool   readonly() const { return f_ && f_->readonly; }
        bool   has_range() const { return f_ && f_->range.has; }
        double range_min() const { return f_ ? f_->range.lo : 0.0; }
        double range_max() const { return f_ ? f_->range.hi : 0.0; }
        /** @brief rosetta::combobox choices — empty when unannotated. */
        std::vector<std::string> choices() const;
        std::vector<Annotation>  annotations() const;

        /** @brief False when the type gate could not marshal it: the field is
         *  still described, so a UI greys it out instead of it vanishing. */
        bool readable() const { return f_ && f_->get; }
        bool writable() const { return f_ && f_->set; }

    private:
        explicit FieldInfo(const dyn::MetaField *f) : f_(f) {}
        const dyn::MetaField *f_ = nullptr;

        friend struct detail::Access;
        friend class ClassInfo;
        friend class Instance;
    };

    /** @brief One parameter of a method, constructor or free function. */
    class ParamInfo {
    public:
        ParamInfo() = default;

        /** @brief "argN" until parameter names are reflected — a dialog
         *  generator should fall back to the position when it starts with
         *  "arg". */
        std::string name() const { return p_ ? p_->name : ""; }
        TypeInfo    type() const { return TypeInfo(p_ ? p_->type : nullptr); }
        bool        is_ref() const { return p_ && p_->is_ref; }
        bool        is_mutable_ref() const { return p_ && p_->is_mutable_ref; }
        /** @brief Manifest "out_params": joins the return rather than the call. */
        bool is_out() const { return p_ && p_->is_out; }

    private:
        explicit ParamInfo(const dyn::MetaParam *p) : p_(p) {}
        const dyn::MetaParam *p_ = nullptr;

        friend struct detail::Access;
        friend class MethodInfo;
        friend class CtorInfo;
        friend class FunctionInfo;
    };

    class MethodInfo {
    public:
        MethodInfo() = default;

        bool        valid() const { return m_ != nullptr; }
        std::string name() const { return m_ ? m_->name : ""; }
        std::string doc() const { return m_ ? m_->doc : ""; }
        TypeInfo    ret() const { return TypeInfo(m_ ? m_->ret : nullptr); }

        std::vector<ParamInfo>  params() const;
        std::vector<Annotation> annotations() const;

        bool is_static() const { return m_ && m_->is_static; }
        bool is_const() const { return m_ && m_->is_const; }
        bool is_virtual() const { return m_ && m_->is_virtual; }

        /** @brief Position in the same-named set. The dynamic model keeps every
         *  overload, unlike the name-keyed backends — a menu builder can show
         *  "at() [2 of 3]" and a diagnostic can name the losers. */
        long long overload_index() const { return m_ ? (long long)m_->overload_index : 0; }
        long long overload_count() const { return m_ ? (long long)m_->overload_count : 0; }

        bool        callable() const { return m_ && m_->invoke; }
        /** @brief Non-empty exactly when callable() is false. */
        std::string skip_reason() const { return m_ ? m_->skip_reason : ""; }

    private:
        explicit MethodInfo(const dyn::MetaMethod *m) : m_(m) {}
        const dyn::MetaMethod *m_ = nullptr;

        friend struct detail::Access;
        friend class ClassInfo;
        friend class Instance;
    };

    class CtorInfo {
    public:
        CtorInfo() = default;

        bool                   valid() const { return c_ != nullptr; }
        std::string            doc() const { return c_ ? c_->doc : ""; }
        std::vector<ParamInfo> params() const;

    private:
        explicit CtorInfo(const dyn::MetaCtor *c) : c_(c) {}
        const dyn::MetaCtor *c_ = nullptr;

        friend struct detail::Access;
        friend class ClassInfo;
    };

    // =====================================================================
    // Class / Enum / Function
    // =====================================================================

    class ClassInfo {
    public:
        ClassInfo() = default;

        bool        valid() const { return k_ != nullptr; }
        std::string name() const { return k_ ? k_->name : ""; }
        std::string qualified() const { return k_ ? k_->qualified : ""; }
        std::string doc() const { return k_ ? k_->doc : ""; }
        bool        is_abstract() const { return k_ && k_->is_abstract; }
        /** @brief False when the destructor is not public: such a class can be
         *  borrowed and inspected, never owned. */
        bool is_destructible() const { return k_ && k_->destroy; }

        std::vector<ClassInfo>  bases() const;
        std::vector<FieldInfo>  fields() const;
        std::vector<MethodInfo> methods() const;
        std::vector<CtorInfo>   ctors() const;
        std::vector<Annotation> annotations() const;

        FieldInfo               field(const std::string &n) const;
        MethodInfo              method(const std::string &n) const; // first overload
        std::vector<MethodInfo> overloads(const std::string &n) const;
        bool                    derives_from(const ClassInfo &b) const;

        /** @brief Construct an instance; the handle owns it. */
        Outcome create(const std::vector<Value> &args = {}) const;
        /** @brief Call a static method by name, overload-resolved on args. */
        Outcome call_static(const std::string &name, const std::vector<Value> &args = {}) const;

        /** @brief Why no overload of `name` accepts `args` — the diagnostic the
         *  name-keyed backends structurally cannot produce, having thrown the
         *  siblings away at generation time. Empty when one does match. */
        std::string why_no_match(const std::string &name, const std::vector<Value> &args) const;

    private:
        explicit ClassInfo(const dyn::MetaClass *k) : k_(k) {}
        const dyn::MetaClass *k_ = nullptr;

        friend struct detail::Access;
        friend class TypeInfo;
        friend class Instance;
        friend ClassInfo         find_class(const std::string &);
        friend std::vector<ClassInfo> classes();
    };

    class EnumInfo {
    public:
        EnumInfo() = default;

        bool                     valid() const { return e_ != nullptr; }
        std::string              name() const { return e_ ? e_->name : ""; }
        std::string              qualified() const { return e_ ? e_->qualified : ""; }
        std::string              underlying() const { return e_ ? e_->underlying : "int"; }
        std::vector<std::string> names() const;
        std::vector<long long>   values() const;

    private:
        explicit EnumInfo(const dyn::MetaEnum *e) : e_(e) {}
        const dyn::MetaEnum *e_ = nullptr;

        friend struct detail::Access;
        friend EnumInfo               find_enum(const std::string &);
        friend std::vector<EnumInfo>  enums();
    };

    class FunctionInfo {
    public:
        FunctionInfo() = default;

        bool                   valid() const { return f_ != nullptr; }
        std::string            name() const { return f_ ? f_->name : ""; }
        std::string            qualified() const { return f_ ? f_->qualified : ""; }
        std::string            doc() const { return f_ ? f_->doc : ""; }
        TypeInfo               ret() const { return TypeInfo(f_ ? f_->ret : nullptr); }
        std::vector<ParamInfo> params() const;

        long long overload_index() const { return f_ ? (long long)f_->overload_index : 0; }
        long long overload_count() const { return f_ ? (long long)f_->overload_count : 0; }

        bool        callable() const { return f_ && f_->invoke; }
        std::string skip_reason() const { return f_ ? f_->skip_reason : ""; }

        Outcome call(const std::vector<Value> &args = {}) const;

    private:
        explicit FunctionInfo(const dyn::MetaFunction *f) : f_(f) {}
        const dyn::MetaFunction *f_ = nullptr;

        friend struct detail::Access;
        friend FunctionInfo               find_function(const std::string &);
        friend std::vector<FunctionInfo>  functions();
        friend std::vector<FunctionInfo>  function_overloads(const std::string &);
    };

    // =====================================================================
    // Instance — the handle a script holds
    // =====================================================================

    /**
     * @brief An object plus its class: get / set / call by name.
     *
     * Ownership rides in dyn::ObjectRef::owner, so a borrowed sub-object
     * returned by a getter PINS its parent — the dangling-handle hole that
     * node's Wrap<T> and wasm's raw-pointer returns have today.
     */
    class Instance {
    public:
        Instance() = default;

        bool      valid() const { return obj_.valid(); }
        ClassInfo cls() const { return ClassInfo(obj_.meta()); }

        Outcome get(const std::string &field) const;
        Outcome set(const std::string &field, const Value &v) const;
        Outcome call(const std::string &method, const std::vector<Value> &args = {}) const;

        /** @brief As a Value, sharing this handle's ownership — so an instance
         *  can be passed as an argument to another call. */
        Value as_value() const { return Value(obj_.as_any()); }

        std::string to_string() const;

    private:
        explicit Instance(dyn::Object o) : obj_(std::move(o)) {}
        dyn::Object obj_;

        friend struct detail::Access;
        friend class Value;
        friend class ClassInfo;
    };

    // =====================================================================
    // Registry — free functions, so a script says script.classes()
    // =====================================================================

    std::vector<ClassInfo>    classes();
    std::vector<EnumInfo>     enums();
    std::vector<FunctionInfo> functions();

    ClassInfo    find_class(const std::string &name);   // bound OR qualified name
    EnumInfo     find_enum(const std::string &name);
    FunctionInfo find_function(const std::string &name);

    std::vector<FunctionInfo> function_overloads(const std::string &name);

    /** @brief Convenience: find_class(name).create(args) in one call. */
    Outcome create(const std::string &class_name, const std::vector<Value> &args = {});

    // =====================================================================
    // visit — the half of a type caster that is NOT language-specific
    // =====================================================================

    /**
     * @brief Dispatch a Value to exactly one method of `sink`, by kind.
     *
     * A type caster has two halves. Reading a HOST value is irreducibly
     * per-language: only Python knows what PyFloat_Check means. Writing one is
     * the same shape everywhere — switch on the kind, call the host's
     * constructor — and that half is here, so the four casters in
     * <rosetta/runtime/script_casters.h> cannot silently disagree about it.
     *
     * The policy decisions this centralises are the ones that would otherwise
     * drift: an integral number is on_int and a floating one is on_real
     * (the split lives in TypeDesc, not in the box); an enum arrives as its
     * integer WITH its TypeInfo, so a sink that wants the enumerator name can
     * reach it; and Kind::unknown — no backend gate claimed the type — becomes
     * on_unknown with the debug rendering rather than an error, because the
     * caller asked for a value and there IS one, it just has no host shape.
     *
     * Sink must provide, all returning the same type R:
     *
     *     R on_none();
     *     R on_bool(bool);
     *     R on_int(long long);
     *     R on_real(double);
     *     R on_string(const std::string &);
     *     R on_enum(long long, const TypeInfo &);
     *     R on_list(const std::vector<Value> &);
     *     R on_object(const Instance &);
     *     R on_unknown(const std::string &rendered);
     *
     * Recursing for a list means calling visit() again with the same sink —
     * `visit(element, *this)` from inside on_list.
     */
    template <class Sink> decltype(auto) visit(const Value &v, Sink &&sink);

} // namespace rosetta::script

// Bodies live in a separate header so the metadata surface above stays
// readable, exactly as <rosetta/dynamic.h> does with inline/dynamic.hxx.
#include "inline/script.hxx"
