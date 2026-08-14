// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Bodies for <rosetta/dynamic.h>. Not standalone.

#pragma once

#include <algorithm>
#include <sstream>

namespace rosetta::dyn {

    // ---------------------------------------------------------------------
    // Types
    // ---------------------------------------------------------------------

    inline const char *kind_name(Kind k) {
        switch (k) {
        case Kind::unknown:
            return "unknown";
        case Kind::void_:
            return "void";
        case Kind::boolean:
            return "boolean";
        case Kind::number:
            return "number";
        case Kind::string:
            return "string";
        case Kind::enum_:
            return "enum";
        case Kind::object:
            return "object";
        case Kind::vector:
            return "vector";
        }
        return "unknown";
    }

    // The builtin descriptors a hand-built Any gets when the caller has no
    // TypeDesc to hand (a script pushing a literal). Generated code always
    // passes its own, so these are the fallback path, not the common one.
    inline const TypeDesc *builtin_void() {
        static const TypeDesc t{Kind::void_, "void"};
        return &t;
    }
    inline const TypeDesc *builtin_bool() {
        static const TypeDesc t{Kind::boolean, "bool"};
        return &t;
    }
    inline const TypeDesc *builtin_int() {
        static const TypeDesc t{Kind::number, "long long", "", /*integral=*/true};
        return &t;
    }
    inline const TypeDesc *builtin_double() {
        static const TypeDesc t{Kind::number, "double", "", /*integral=*/false};
        return &t;
    }
    inline const TypeDesc *builtin_string() {
        static const TypeDesc t{Kind::string, "std::string"};
        return &t;
    }

    inline bool same_type(const TypeDesc *a, const TypeDesc *b) {
        if (a == b) {
            return true;
        }
        if (!a || !b || a->kind != b->kind) {
            return false;
        }
        if (a->kind == Kind::object || a->kind == Kind::enum_) {
            return std::string_view(a->object) == b->object;
        }
        if (a->kind == Kind::number) {
            return a->integral == b->integral;
        }
        if (a->kind == Kind::vector) {
            return same_type(a->element, b->element);
        }
        return true;
    }

    // ---------------------------------------------------------------------
    // Any
    // ---------------------------------------------------------------------

    inline Any Any::none() { return Any(builtin_void(), {}); }

    inline Any Any::boolean(bool v) { return Any(builtin_bool(), std::any(v)); }

    inline Any Any::integer(long long v, const TypeDesc *t) {
        return Any(t ? t : builtin_int(), std::any(v));
    }

    inline Any Any::real(double v, const TypeDesc *t) {
        return Any(t ? t : builtin_double(), std::any(v));
    }

    inline Any Any::text(std::string v, const TypeDesc *t) {
        return Any(t ? t : builtin_string(), std::any(std::move(v)));
    }

    inline Any Any::enumeration(long long v, const TypeDesc *t) { return Any(t, std::any(v)); }

    inline Any Any::list(std::vector<Any> v, const TypeDesc *t) {
        return Any(t, std::any(std::move(v)));
    }

    inline Any Any::object(void *p, const MetaClass *c, std::shared_ptr<void> owner,
                           const TypeDesc *t) {
        return Any(t, std::any(ObjectRef{p, c, std::move(owner)}));
    }

    inline Any Any::opaque(std::any v, const TypeDesc *t) { return Any(t, std::move(v)); }

    inline Kind Any::kind() const { return type_ ? type_->kind : Kind::unknown; }

    namespace detail {

        [[noreturn]] inline void wrong_box(const char *want, const Any &v) {
            throw Error(std::string("Any holds no ") + want + " (kind " + kind_name(v.kind()) +
                        ", type " + (v.type() ? v.type()->spelling : "?") + ")");
        }

        template <class T> const T &unbox(const Any &v, const char *want) {
            if (const T *p = std::any_cast<T>(&v.raw())) {
                return *p;
            }
            wrong_box(want, v);
        }

    } // namespace detail

    inline bool Any::as_bool() const { return detail::unbox<bool>(*this, "bool"); }

    inline long long Any::as_int() const {
        if (const long long *p = std::any_cast<long long>(&box_)) {
            return *p;
        }
        if (const double *p = std::any_cast<double>(&box_)) {
            return static_cast<long long>(*p);
        }
        if (const bool *p = std::any_cast<bool>(&box_)) {
            return *p ? 1 : 0;
        }
        detail::wrong_box("integer", *this);
    }

    inline double Any::as_number() const {
        if (const double *p = std::any_cast<double>(&box_)) {
            return *p;
        }
        if (const long long *p = std::any_cast<long long>(&box_)) {
            return static_cast<double>(*p);
        }
        if (const bool *p = std::any_cast<bool>(&box_)) {
            return *p ? 1.0 : 0.0;
        }
        detail::wrong_box("number", *this);
    }

    inline const std::string &Any::as_string() const {
        return detail::unbox<std::string>(*this, "string");
    }

    inline const std::vector<Any> &Any::as_list() const {
        return detail::unbox<std::vector<Any>>(*this, "sequence");
    }

    inline const ObjectRef &Any::as_object() const {
        return detail::unbox<ObjectRef>(*this, "object");
    }

    inline std::string Any::to_string() const {
        std::ostringstream os;
        switch (kind()) {
        case Kind::void_:
            return "void";
        case Kind::boolean:
            return as_bool() ? "true" : "false";
        case Kind::number:
            if (type_ && type_->integral) {
                os << as_int();
            } else {
                os << as_number();
            }
            return os.str();
        case Kind::string:
            return "\"" + as_string() + "\"";
        case Kind::enum_: {
            const long long v = as_int();
            if (type_) {
                for (std::size_t i = 0; i < type_->n_enumerators; ++i) {
                    if (type_->enumerators[i].value == v) {
                        return std::string(type_->object) + "::" + type_->enumerators[i].name;
                    }
                }
            }
            os << v;
            return os.str();
        }
        case Kind::vector: {
            os << "[";
            const auto &l = as_list();
            for (std::size_t i = 0; i < l.size(); ++i) {
                os << (i ? ", " : "") << l[i].to_string();
            }
            os << "]";
            return os.str();
        }
        case Kind::object: {
            const ObjectRef &r = as_object();
            // Three distinguishable states, because they are three different
            // lifetime contracts: this handle keeps the object alive; this
            // handle keeps its PARENT alive (a sub-object pin); this handle
            // guarantees nothing.
            const char *own = !r.owner              ? "borrowed"
                              : r.owner.get() == r.ptr ? "owned"
                                                       : "pinned";
            os << "<" << (r.cls ? r.cls->qualified : "object") << " @" << r.ptr << " " << own
               << ">";
            return os.str();
        }
        case Kind::unknown:
            return std::string("<opaque ") + (type_ ? type_->spelling : "?") + ">";
        }
        return "?";
    }

    // ---------------------------------------------------------------------
    // ArgList
    // ---------------------------------------------------------------------

    inline ArgList::ArgList(std::initializer_list<Any> vs) : values_(vs), names_(vs.size()) {}

    inline const Any &ArgList::operator[](std::size_t i) const {
        if (i >= values_.size()) {
            throw Error("argument index " + std::to_string(i) + " out of range (" +
                        std::to_string(values_.size()) + " given)");
        }
        return values_[i];
    }

    inline const Any *ArgList::by_name(std::string_view n) const {
        for (std::size_t i = 0; i < names_.size(); ++i) {
            if (names_[i] == n) {
                return &values_[i];
            }
        }
        return nullptr;
    }

    inline void ArgList::add(Any v) {
        values_.push_back(std::move(v));
        names_.emplace_back();
    }

    inline void ArgList::add(std::string name, Any v) {
        values_.push_back(std::move(v));
        names_.push_back(std::move(name));
    }

    // ---------------------------------------------------------------------
    // MetaClass lookup
    // ---------------------------------------------------------------------

    inline const MetaField *MetaClass::field(std::string_view n) const {
        for (std::size_t i = 0; i < n_fields; ++i) {
            if (n == fields[i].name) {
                return &fields[i];
            }
        }
        for (std::size_t b = 0; b < n_bases; ++b) {
            if (const MetaField *f = bases[b]->field(n)) {
                return f;
            }
        }
        return nullptr;
    }

    inline const MetaMethod *MetaClass::method(std::string_view n) const {
        for (std::size_t i = 0; i < n_methods; ++i) {
            if (n == methods[i].name) {
                return &methods[i];
            }
        }
        for (std::size_t b = 0; b < n_bases; ++b) {
            if (const MetaMethod *m = bases[b]->method(n)) {
                return m;
            }
        }
        return nullptr;
    }

    inline std::vector<const MetaMethod *> MetaClass::overloads(std::string_view n) const {
        std::vector<const MetaMethod *> out;
        for (std::size_t i = 0; i < n_methods; ++i) {
            if (n == methods[i].name) {
                out.push_back(&methods[i]);
            }
        }
        // A derived declaration hides the whole base set of that name — the
        // same shadowing rule walk<T> applies at compile time (drop_reason::
        // hidden_by_derived), kept here so dynamic lookup and generated
        // bindings agree about which overloads exist.
        if (out.empty()) {
            for (std::size_t b = 0; b < n_bases; ++b) {
                out = bases[b]->overloads(n);
                if (!out.empty()) {
                    break;
                }
            }
        }
        return out;
    }

    inline bool MetaClass::derives_from(const MetaClass *b) const {
        if (!b) {
            return false;
        }
        for (std::size_t i = 0; i < n_bases; ++i) {
            if (bases[i] == b || bases[i]->derives_from(b)) {
                return true;
            }
        }
        return false;
    }

    // ---------------------------------------------------------------------
    // Overload matching
    // ---------------------------------------------------------------------

    inline Match match(const TypeDesc *p, const Any &a) {
        if (!p) {
            return Match::none;
        }
        const Kind ak = a.kind();
        switch (p->kind) {
        case Kind::boolean:
            if (ak == Kind::boolean) {
                return Match::exact;
            }
            return ak == Kind::number ? Match::convert : Match::none;

        case Kind::number:
            if (ak == Kind::number) {
                const bool ai = a.type() && a.type()->integral;
                return ai == p->integral ? Match::exact : Match::promote;
            }
            if (ak == Kind::enum_ || ak == Kind::boolean) {
                return Match::convert;
            }
            return Match::none;

        case Kind::string:
            return ak == Kind::string ? Match::exact : Match::none;

        case Kind::enum_:
            if (ak == Kind::enum_) {
                return same_type(a.type(), p) ? Match::exact : Match::convert;
            }
            return ak == Kind::number ? Match::convert : Match::none;

        case Kind::object: {
            if (ak != Kind::object) {
                return Match::none;
            }
            const MetaClass *ac = a.as_object().cls;
            if (!ac || !p->cls) {
                return Match::none;
            }
            if (ac == p->cls) {
                return Match::exact;
            }
            // Derived-to-base is a real conversion but always worse than an
            // exact hit, so an overload taking the derived type wins.
            return ac->derives_from(p->cls) ? Match::promote : Match::none;
        }

        case Kind::vector: {
            if (ak != Kind::vector) {
                return Match::none;
            }
            const auto &l = a.as_list();
            if (l.empty()) {
                return Match::exact; // an empty list fits any sequence
            }
            Match worst = Match::exact;
            for (const Any &e : l) {
                const Match m = match(p->element, e);
                if (m == Match::none) {
                    return Match::none;
                }
                worst = static_cast<int>(m) < static_cast<int>(worst) ? m : worst;
            }
            return worst;
        }

        case Kind::unknown:
            // An opaque parameter accepts an opaque argument and nothing else:
            // the marshaller has no idea what is inside either one.
            return ak == Kind::unknown ? Match::convert : Match::none;

        case Kind::void_:
            return Match::none;
        }
        return Match::none;
    }

    namespace detail {

        // Parameters the CALLER supplies: out-params are filled by the callee
        // and never appear in the argument list (GenParam::is_out).
        inline std::size_t input_arity(const MetaParam *ps, std::size_t n) {
            std::size_t k = 0;
            for (std::size_t i = 0; i < n; ++i) {
                k += ps[i].is_out ? 0 : 1;
            }
            return k;
        }

        inline std::string signature_of(const MetaParam *ps, std::size_t n) {
            std::string s = "(";
            bool        first = true;
            for (std::size_t i = 0; i < n; ++i) {
                if (ps[i].is_out) {
                    continue;
                }
                s += (first ? "" : ", ");
                s += ps[i].type ? ps[i].type->spelling : "?";
                first = false;
            }
            return s + ")";
        }

        inline std::string given_of(const ArgList &a) {
            std::string s = "(";
            for (std::size_t i = 0; i < a.size(); ++i) {
                s += (i ? ", " : "");
                s += a[i].type() ? a[i].type()->spelling : kind_name(a[i].kind());
            }
            return s + ")";
        }

        // Shared scoring core: returns the summed Match, or -1 for "rejected",
        // appending a one-line reason to `notes` either way.
        inline int score_call(const MetaParam *ps, std::size_t n, const ArgList &args,
                              const char *skip_reason, bool callable, std::string &notes,
                              const char *label) {
            if (!callable) {
                notes += std::string("\n  ") + label + signature_of(ps, n) +
                         " — not callable: " + (skip_reason && *skip_reason ? skip_reason : "no thunk");
                return -1;
            }
            const std::size_t need = input_arity(ps, n);
            if (args.size() != need) {
                notes += std::string("\n  ") + label + signature_of(ps, n) + " — takes " +
                         std::to_string(need) + " argument(s), " + std::to_string(args.size()) +
                         " given";
                return -1;
            }
            int         score = 0;
            std::size_t ai    = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (ps[i].is_out) {
                    continue;
                }
                const Match m = match(ps[i].type, args[ai]);
                if (m == Match::none) {
                    notes += std::string("\n  ") + label + signature_of(ps, n) + " — argument " +
                             std::to_string(ai + 1) + " (" + args[ai].to_string() +
                             ") is not a " + (ps[i].type ? ps[i].type->spelling : "?");
                    return -1;
                }
                score += static_cast<int>(m);
                ++ai;
            }
            return score;
        }

    } // namespace detail

    inline const MetaMethod *resolve(const MetaClass &k, std::string_view name,
                                     const ArgList &args, std::string *why) {
        const std::vector<const MetaMethod *> cands = k.overloads(name);
        if (cands.empty()) {
            if (why) {
                *why = std::string(k.name) + " has no method '" + std::string(name) + "'";
            }
            return nullptr;
        }

        const MetaMethod *best      = nullptr;
        int               best_score = -1;
        bool              ambiguous = false;
        std::string       notes;

        for (const MetaMethod *m : cands) {
            const int s = detail::score_call(m->params, m->n_params, args, m->skip_reason,
                                             m->invoke != nullptr, notes, m->name);
            if (s < 0) {
                continue;
            }
            if (s > best_score) {
                best_score = s;
                best       = m;
                ambiguous  = false;
            } else if (s == best_score) {
                ambiguous = true;
            }
        }

        if (!best) {
            if (why) {
                *why = std::string(k.name) + "::" + std::string(name) + detail::given_of(args) +
                       " matched no overload of " + std::to_string(cands.size()) + ":" + notes;
            }
            return nullptr;
        }
        if (ambiguous) {
            if (why) {
                *why = std::string(k.name) + "::" + std::string(name) + detail::given_of(args) +
                       " is ambiguous between equally good overloads:" + notes;
            }
            return nullptr;
        }
        return best;
    }

    // ---------------------------------------------------------------------
    // Object
    // ---------------------------------------------------------------------

    inline Result Object::create(const MetaClass &k, const ArgList &args) {
        if (k.is_abstract) {
            return {Any::none(), std::string(k.name) + " is abstract"};
        }
        if (!k.destroy) {
            return {Any::none(), std::string(k.name) +
                                     " has no public destructor — it can be borrowed, not created"};
        }

        const MetaCtor *best      = nullptr;
        int             best_score = -1;
        bool            ambiguous = false;
        std::string     notes;

        for (std::size_t i = 0; i < k.n_ctors; ++i) {
            const MetaCtor &c = k.ctors[i];
            const int       s = detail::score_call(c.params, c.n_params, args, "", c.construct,
                                                   notes, k.name);
            if (s < 0) {
                continue;
            }
            if (s > best_score) {
                best_score = s;
                best       = &c;
                ambiguous  = false;
            } else if (s == best_score) {
                ambiguous = true;
            }
        }

        if (!best) {
            return {Any::none(), std::string(k.name) + detail::given_of(args) +
                                     " matched no constructor:" + notes};
        }
        if (ambiguous) {
            return {Any::none(), std::string(k.name) + detail::given_of(args) + " is ambiguous"};
        }

        void *p = nullptr;
        try {
            p = best->construct(args);
        } catch (const std::exception &e) {
            return {Any::none(), std::string("constructing ") + k.name + ": " + e.what()};
        }
        if (!p) {
            return {Any::none(), std::string("constructing ") + k.name + " returned null"};
        }

        Object o;
        o.ref_.ptr   = p;
        o.ref_.cls   = &k;
        o.ref_.owner = std::shared_ptr<void>(p, k.destroy);
        return {o.as_any(), {}};
    }

    inline Object Object::borrow(const MetaClass &k, void *p) {
        Object o;
        o.ref_.ptr = p;
        o.ref_.cls = &k;
        return o;
    }

    inline Object Object::adopt(const MetaClass &k, void *p, std::shared_ptr<void> owner) {
        Object o;
        o.ref_.ptr   = p;
        o.ref_.cls   = &k;
        o.ref_.owner = std::move(owner);
        return o;
    }

    inline Any Object::as_any() const {
        return Any::object(ref_.ptr, ref_.cls, ref_.owner, ref_.cls ? ref_.cls->self : nullptr);
    }

    inline Result Object::get(std::string_view name) const {
        if (!valid()) {
            return {Any::none(), "null object"};
        }
        const MetaField *f = ref_.cls->field(name);
        if (!f) {
            return {Any::none(), std::string(ref_.cls->name) + " has no field '" +
                                     std::string(name) + "'"};
        }
        if (!f->get) {
            return {Any::none(), std::string(ref_.cls->name) + "::" + std::string(name) +
                                     " is not readable across the boundary"};
        }
        try {
            return {f->get(ref_, {}), {}};
        } catch (const std::exception &e) {
            return {Any::none(), std::string("reading ") + f->name + ": " + e.what()};
        }
    }

    inline Result Object::set(std::string_view name, const Any &v) const {
        if (!valid()) {
            return {Any::none(), "null object"};
        }
        const MetaField *f = ref_.cls->field(name);
        if (!f) {
            return {Any::none(), std::string(ref_.cls->name) + " has no field '" +
                                     std::string(name) + "'"};
        }
        if (f->readonly || !f->set) {
            return {Any::none(), std::string(ref_.cls->name) + "::" + std::string(name) +
                                     " is read-only"};
        }
        if (match(f->type, v) == Match::none) {
            return {Any::none(), std::string(f->name) + " expects " +
                                     (f->type ? f->type->spelling : "?") + ", got " +
                                     v.to_string()};
        }
        // range{lo,hi} is enforced HERE, once, instead of in each backend's
        // setter — the same annotation the REST visitor turns into a 400 and
        // the ImGui runtime turns into a slider bound.
        if (f->range.has && (v.kind() == Kind::number || v.kind() == Kind::enum_)) {
            const double d = v.as_number();
            if (d < f->range.lo || d > f->range.hi) {
                std::ostringstream os;
                os << f->name << " = " << d << " is outside [" << f->range.lo << ", "
                   << f->range.hi << "]";
                return {Any::none(), os.str()};
            }
        }
        try {
            f->set(ref_, ArgList{v});
            return {Any::none(), {}};
        } catch (const std::exception &e) {
            return {Any::none(), std::string("writing ") + f->name + ": " + e.what()};
        }
    }

    inline Result Object::call(std::string_view name, const ArgList &args) const {
        if (!valid()) {
            return {Any::none(), "null object"};
        }
        std::string       why;
        const MetaMethod *m = resolve(*ref_.cls, name, args, &why);
        if (!m) {
            return {Any::none(), why};
        }
        if (m->is_static) {
            return call_static(*ref_.cls, name, args);
        }
        try {
            return {m->invoke(ref_, args), {}};
        } catch (const std::exception &e) {
            return {Any::none(), std::string(ref_.cls->name) + "::" + m->name + ": " + e.what()};
        }
    }

    inline Result call_static(const MetaClass &k, std::string_view name, const ArgList &args) {
        std::string       why;
        const MetaMethod *m = resolve(k, name, args, &why);
        if (!m) {
            return {Any::none(), why};
        }
        if (!m->is_static) {
            return {Any::none(), std::string(k.name) + "::" + m->name + " needs a receiver"};
        }
        try {
            return {m->invoke(ObjectRef{}, args), {}};
        } catch (const std::exception &e) {
            return {Any::none(), std::string(k.name) + "::" + m->name + ": " + e.what()};
        }
    }

    inline Result call_function(const MetaFunction &f, const ArgList &args) {
        std::string notes;
        if (detail::score_call(f.params, f.n_params, args, f.skip_reason, f.invoke != nullptr,
                               notes, f.name) < 0) {
            return {Any::none(), std::string(f.qualified) + detail::given_of(args) +
                                     " does not match:" + notes};
        }
        try {
            return {f.invoke(ObjectRef{}, args), {}};
        } catch (const std::exception &e) {
            return {Any::none(), std::string(f.qualified) + ": " + e.what()};
        }
    }

    // ---------------------------------------------------------------------
    // Registry
    // ---------------------------------------------------------------------

    inline Registry &Registry::instance() {
        static Registry r;
        return r;
    }

    inline void Registry::add_class(const MetaClass *k) {
        if (k && std::find(classes_.begin(), classes_.end(), k) == classes_.end()) {
            classes_.push_back(k);
        }
    }

    inline void Registry::add_enum(const MetaEnum *e) {
        if (e && std::find(enums_.begin(), enums_.end(), e) == enums_.end()) {
            enums_.push_back(e);
        }
    }

    inline void Registry::add_function(const MetaFunction *f) {
        if (f && std::find(functions_.begin(), functions_.end(), f) == functions_.end()) {
            functions_.push_back(f);
        }
    }

    inline const MetaClass *Registry::find_class(std::string_view n) const {
        for (const MetaClass *k : classes_) {
            if (n == k->qualified) {
                return k;
            }
        }
        for (const MetaClass *k : classes_) {
            if (n == k->name) {
                return k;
            }
        }
        return nullptr;
    }

    inline const MetaEnum *Registry::find_enum(std::string_view n) const {
        for (const MetaEnum *e : enums_) {
            if (n == e->qualified || n == e->name) {
                return e;
            }
        }
        return nullptr;
    }

    inline const MetaFunction *Registry::find_function(std::string_view n) const {
        for (const MetaFunction *f : functions_) {
            if (n == f->qualified || n == f->name) {
                return f;
            }
        }
        return nullptr;
    }

    inline std::vector<const MetaFunction *>
    Registry::function_overloads(std::string_view n) const {
        std::vector<const MetaFunction *> out;
        for (const MetaFunction *f : functions_) {
            if (n == f->qualified || n == f->name) {
                out.push_back(f);
            }
        }
        return out;
    }

    inline void Registry::link() {
        // Within one generated module every TypeDesc::cls is already set: the
        // emitter forward-declares its MetaClass objects, so `&kMesh` is a
        // constant expression at static-init time. What CANNOT be resolved then
        // is a CROSS-MODULE reference — module A returning a class bound by
        // module B — because B's tables may not be loaded yet. Those descriptors
        // carry `object` (the qualified name) with a null `cls`, and this pass
        // fills them in once both modules have registered.
        //
        // TypeDesc is therefore the one part of the emitted metadata that is
        // mutable (it lands in .data, not .rodata); the MetaField / MetaMethod /
        // MetaParam arrays stay const.
        auto relink = [this](const TypeDesc *ct) {
            for (const TypeDesc *t = ct; t; t = t->element) {
                if (t->kind == Kind::object && !t->cls && *t->object) {
                    if (const MetaClass *k = find_class(t->object)) {
                        const_cast<TypeDesc *>(t)->cls = k;
                    }
                }
            }
        };
        for (const MetaClass *k : classes_) {
            for (std::size_t i = 0; i < k->n_fields; ++i) {
                relink(k->fields[i].type);
            }
            for (std::size_t i = 0; i < k->n_methods; ++i) {
                relink(k->methods[i].ret);
                for (std::size_t j = 0; j < k->methods[i].n_params; ++j) {
                    relink(k->methods[i].params[j].type);
                }
            }
            for (std::size_t i = 0; i < k->n_ctors; ++i) {
                for (std::size_t j = 0; j < k->ctors[i].n_params; ++j) {
                    relink(k->ctors[i].params[j].type);
                }
            }
        }
        for (const MetaFunction *f : functions_) {
            relink(f->ret);
            for (std::size_t j = 0; j < f->n_params; ++j) {
                relink(f->params[j].type);
            }
        }
    }

} // namespace rosetta::dyn
