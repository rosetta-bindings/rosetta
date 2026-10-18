// Copyright (c) fmaerten@gmail.com
// License: MIT

// Bodies for <rosetta/dynamic.h>. Not standalone.

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <regex>
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
    /**
     * @brief The descriptor an untyped list gets: "a sequence of something".
     *
     * `element` is deliberately left null, and that is not a gap. A host
     * language hands over a heterogeneous sequence and there is no single C++
     * element type to name — but nothing needs one: match()'s vector case
     * recurses into each ELEMENT's own descriptor against the parameter's
     * element, and never dereferences the argument's. What the argument must
     * carry is Kind::vector, so that the case is reached at all.
     */
    inline const TypeDesc *builtin_list() {
        static const TypeDesc t{Kind::vector, "vector<?>"};
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
        // Substitute a builtin the way integer() / real() / text() do. Without
        // it a null `t` leaves kind() reading Kind::unknown — as it reads
        // type_->kind — and match() then rejects the list against EVERY vector
        // parameter, so a script's `[1, 2, 3]` could never reach a
        // std::vector<int>. The two callers that pass a descriptor (make_any
        // in dynamic_cast.hxx, and generated thunks) are unaffected; the ones
        // that cannot are language wrappers boxing a host-language sequence,
        // which is the case this whole header exists to serve.
        return Any(t ? t : builtin_list(), std::any(std::move(v)));
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

        // Parameters the caller MUST supply: the input ones, minus the trailing
        // run that carries a default (MetaParam::default_value).
        inline std::size_t required_arity(const MetaParam *ps, std::size_t n) {
            std::size_t req = input_arity(ps, n);
            for (std::size_t i = n; i-- > 0;) {
                if (ps[i].is_out) {
                    continue;
                }
                if (!ps[i].default_value) {
                    break;
                }
                --req;
            }
            return req;
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
            const std::size_t req  = required_arity(ps, n);
            if (args.size() < req || args.size() > need) {
                notes += std::string("\n  ") + label + signature_of(ps, n) + " — takes " +
                         (req == need ? std::to_string(need)
                                      : std::to_string(req) + " to " + std::to_string(need)) +
                         " argument(s), " + std::to_string(args.size()) + " given";
                return -1;
            }
            int         score = 0;
            std::size_t ai    = 0;
            for (std::size_t i = 0; i < n && ai < args.size(); ++i) {
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

        // ---- defaults ------------------------------------------------------

        /** @brief A MetaParam::default_value text as an Any of the parameter's type. */
        inline Any default_any(const MetaParam &p) {
            const std::string d = p.default_value ? p.default_value : "";
            const TypeDesc   *t = p.type;
            if (d == "true" || d == "false") {
                return Any::boolean(d == "true");
            }
            if (d.size() >= 2 && d.front() == '"' && d.back() == '"') {
                const std::string text = d.substr(1, d.size() - 2);
                if (t && t->kind == Kind::enum_) {
                    for (std::size_t i = 0; i < t->n_enumerators; ++i) {
                        if (text == t->enumerators[i].name) {
                            return Any::enumeration(t->enumerators[i].value, t);
                        }
                    }
                    throw Error("default '" + text + "' is not a " + t->spelling);
                }
                return Any::text(text, t && t->kind == Kind::string ? t : nullptr);
            }
            const double v = std::strtod(d.c_str(), nullptr);
            if (t && t->kind == Kind::number && !t->integral) {
                return Any::real(v, t);
            }
            if (t && t->kind == Kind::enum_) {
                return Any::enumeration(static_cast<long long>(v), t);
            }
            return Any::integer(static_cast<long long>(v), t && t->kind == Kind::number ? t : nullptr);
        }

        /** @brief `args` with every omitted trailing parameter filled from its default. */
        inline ArgList complete(const MetaParam *ps, std::size_t n, const ArgList &args) {
            if (args.size() >= input_arity(ps, n)) {
                return args;
            }
            ArgList     out = args;
            std::size_t ai  = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (ps[i].is_out) {
                    continue;
                }
                if (ai++ >= args.size()) {
                    out.add(ps[i].name, default_any(ps[i]));
                }
            }
            return out;
        }

        // ---- expressions ---------------------------------------------------

        struct Val {
            enum K { num, str, boolean } k = num;
            double      d = 0;
            std::string s;
            bool        b = false;

            static Val number(double x) { return {num, x, {}, false}; }
            static Val text(std::string x) { return {str, 0, std::move(x), false}; }
            static Val flag(bool x) { return {boolean, 0, {}, x}; }

            bool truthy() const { return k == boolean ? b : k == num ? d != 0 : !s.empty(); }
        };

        inline Val val_of(const Any &a) {
            switch (a.kind()) {
            case Kind::boolean:
                return Val::flag(a.as_bool());
            case Kind::number:
                return Val::number(a.as_number());
            case Kind::string:
                return Val::text(a.as_string());
            case Kind::enum_: {
                const TypeDesc *t = a.type();
                for (std::size_t i = 0; t && i < t->n_enumerators; ++i) {
                    if (t->enumerators[i].value == a.as_int()) {
                        return Val::text(t->enumerators[i].name);
                    }
                }
                return Val::number(static_cast<double>(a.as_int()));
            }
            case Kind::vector:
                return Val::number(static_cast<double>(a.as_list().size())); // its size
            default:
                throw Error(std::string("cannot use a ") + kind_name(a.kind()) + " in an expression");
            }
        }

        /**
         * @brief Recursive-descent evaluator for the rosetta::requires_ grammar.
         * Throws Error on a syntax error or an unknown name.
         */
        struct Expr {
            std::string_view src;
            std::size_t      i = 0;
            const Object    *self;
            const MetaParam *ps;
            std::size_t      n;
            const ArgList   *args;
            std::string_view bound_name; // a field under assignment: its NEW value
            const Any       *bound_value;

            void ws() {
                while (i < src.size() && std::isspace(static_cast<unsigned char>(src[i]))) {
                    ++i;
                }
            }
            bool eat(std::string_view tok) {
                ws();
                if (src.substr(i, tok.size()) == tok) {
                    i += tok.size();
                    return true;
                }
                return false;
            }
            [[noreturn]] void fail(const std::string &what) const {
                throw Error("in `" + std::string(src) + "`: " + what);
            }

            Val run() {
                Val v = disj();
                ws();
                if (i != src.size()) {
                    fail("unexpected '" + std::string(src.substr(i)) + "'");
                }
                return v;
            }

            Val disj() {
                Val v = conj();
                while (eat("||")) {
                    const Val r = conj();
                    v           = Val::flag(v.truthy() || r.truthy());
                }
                return v;
            }
            Val conj() {
                Val v = cmp();
                while (eat("&&")) {
                    const Val r = cmp();
                    v           = Val::flag(v.truthy() && r.truthy());
                }
                return v;
            }
            Val cmp() {
                Val l = sum();
                for (const char *op : {"<=", ">=", "==", "!=", "<", ">"}) {
                    if (eat(op)) {
                        return compare(l, sum(), op);
                    }
                }
                return l;
            }
            Val compare(const Val &l, const Val &r, std::string_view op) {
                if (l.k == Val::str || r.k == Val::str) {
                    const std::string a = l.k == Val::str ? l.s : as_text(l);
                    const std::string b = r.k == Val::str ? r.s : as_text(r);
                    if (op == "==") return Val::flag(a == b);
                    if (op == "!=") return Val::flag(a != b);
                    fail("strings compare with == and != only");
                }
                const double a = l.k == Val::boolean ? l.b : l.d;
                const double b = r.k == Val::boolean ? r.b : r.d;
                if (op == "<") return Val::flag(a < b);
                if (op == "<=") return Val::flag(a <= b);
                if (op == ">") return Val::flag(a > b);
                if (op == ">=") return Val::flag(a >= b);
                if (op == "==") return Val::flag(a == b);
                return Val::flag(a != b);
            }
            static std::string as_text(const Val &v) {
                if (v.k == Val::boolean) {
                    return v.b ? "true" : "false";
                }
                std::ostringstream os;
                os << v.d;
                return os.str();
            }
            double num(const Val &v) {
                if (v.k == Val::str) {
                    fail("'" + v.s + "' is not a number");
                }
                return v.k == Val::boolean ? v.b : v.d;
            }
            Val sum() {
                Val v = prod();
                for (;;) {
                    if (eat("+")) {
                        v = Val::number(num(v) + num(prod()));
                    } else if (eat("-")) {
                        v = Val::number(num(v) - num(prod()));
                    } else {
                        return v;
                    }
                }
            }
            Val prod() {
                Val v = unary();
                for (;;) {
                    if (eat("*")) {
                        v = Val::number(num(v) * num(unary()));
                    } else if (eat("/")) {
                        v = Val::number(num(v) / num(unary()));
                    } else {
                        return v;
                    }
                }
            }
            Val unary() {
                if (eat("!")) {
                    return Val::flag(!unary().truthy());
                }
                if (eat("-")) {
                    return Val::number(-num(unary()));
                }
                return primary();
            }
            Val primary() {
                ws();
                if (eat("(")) {
                    Val v = disj();
                    if (!eat(")")) {
                        fail("missing ')'");
                    }
                    return v;
                }
                if (i < src.size() && src[i] == '"') {
                    const std::size_t end = src.find('"', i + 1);
                    if (end == std::string_view::npos) {
                        fail("unterminated string");
                    }
                    Val v = Val::text(std::string(src.substr(i + 1, end - i - 1)));
                    i     = end + 1;
                    return v;
                }
                if (i < src.size() &&
                    (std::isdigit(static_cast<unsigned char>(src[i])) || src[i] == '.')) {
                    char        *end = nullptr;
                    const std::string rest(src.substr(i));
                    const double v = std::strtod(rest.c_str(), &end);
                    i += static_cast<std::size_t>(end - rest.c_str());
                    return Val::number(v);
                }
                const std::size_t start = i;
                while (i < src.size() &&
                       (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) {
                    ++i;
                }
                if (start == i) {
                    fail("expected a value");
                }
                const std::string name(src.substr(start, i - start));
                if (eat("(")) {
                    if (!eat(")")) {
                        fail(name + "(): only nullary methods can be called");
                    }
                    if (!self || !self->valid()) {
                        fail(name + "(): no object to call it on");
                    }
                    const Result r = self->call(name);
                    if (!r.ok()) {
                        fail(r.error);
                    }
                    return val_of(r.value);
                }
                return identifier(name);
            }
            Val identifier(const std::string &name) {
                if (name == "true" || name == "false") {
                    return Val::flag(name == "true");
                }
                if (!bound_name.empty() && name == bound_name) {
                    return val_of(*bound_value);
                }
                if (ps && args) {
                    for (std::size_t p = 0, ai = 0; p < n; ++p) {
                        if (ps[p].is_out) {
                            continue;
                        }
                        if (name == ps[p].name) {
                            return val_of(ai < args->size() ? (*args)[ai] : default_any(ps[p]));
                        }
                        ++ai;
                    }
                }
                if (self && self->valid() && self->meta()->field(name)) {
                    const Result r = self->get(name);
                    if (!r.ok()) {
                        fail(r.error);
                    }
                    return val_of(r.value);
                }
                return Val::text(name); // a bare word: an enumerator name, say
            }
        };

        // ---- validation ----------------------------------------------------

        inline double ann_number(const char *v) { return std::strtod(v, nullptr); }

        /**
         * @brief Every annotation-declared constraint on a field value, beyond
         * range and readonly. Empty when `v` passes; else the reason.
         */
        inline std::string check_field(const MetaField &f, const Any &v, const Object &self) {
            const std::string name = f.name;
            // combobox: the choices are the allowed values, not a suggestion.
            if (f.n_choices && v.kind() == Kind::string) {
                bool ok = false;
                for (std::size_t i = 0; i < f.n_choices && !ok; ++i) {
                    ok = v.as_string() == f.choices[i];
                }
                if (!ok) {
                    std::string list;
                    for (std::size_t i = 0; i < f.n_choices; ++i) {
                        list += (i ? ", " : "") + std::string(f.choices[i]);
                    }
                    return name + " = \"" + v.as_string() + "\" is not one of: " + list;
                }
            }
            if (v.kind() == Kind::number || v.kind() == Kind::enum_) {
                const double d = v.as_number();
                std::ostringstream os;
                if (const char *m = f.annotation("min"); m && d < ann_number(m)) {
                    os << name << " = " << d << " is below the minimum " << m;
                    return os.str();
                }
                if (const char *m = f.annotation("max"); m && d > ann_number(m)) {
                    os << name << " = " << d << " is above the maximum " << m;
                    return os.str();
                }
            }
            if (const char *len = f.annotation("length")) {
                std::size_t lo = 0, hi = 0;
                std::sscanf(len, "[%zu, %zu]", &lo, &hi);
                std::size_t size = 0;
                bool        has  = true;
                if (v.kind() == Kind::string) {
                    size = v.as_string().size();
                } else if (v.kind() == Kind::vector) {
                    size = v.as_list().size();
                } else {
                    has = false;
                }
                if (has && (size < lo || size > hi)) {
                    return name + " has length " + std::to_string(size) + ", allowed " +
                           std::to_string(lo) + ".." + std::to_string(hi);
                }
            }
            if (const char *re = f.annotation("regex"); re && v.kind() == Kind::string) {
                if (!std::regex_match(v.as_string(), std::regex(re))) {
                    return name + " = \"" + v.as_string() + "\" does not match " + re;
                }
            }
            if (const char *req = f.annotation("requires")) {
                Expr e{req, 0, &self, nullptr, 0, nullptr, f.name, &v};
                if (!e.run().truthy()) {
                    const char *msg = f.annotation("requires_message");
                    return name + " = " + v.to_string() + " violates `" + req + "`" +
                           (msg && *msg ? std::string(": ") + msg : std::string());
                }
            }
            return {};
        }

        /** @brief A method's rosetta::requires_ precondition, with its (completed) arguments. */
        inline std::string check_call(const MetaMethod &m, const Object &self, const ArgList &args,
                                      const char *owner) {
            const char *req = m.annotation("requires");
            if (!req) {
                return {};
            }
            Expr e{req, 0, &self, m.params, m.n_params, &args, {}, nullptr};
            if (e.run().truthy()) {
                return {};
            }
            const char *msg = m.annotation("requires_message");
            return std::string(owner) + "::" + m.name + ": precondition `" + req + "` failed" +
                   (msg && *msg ? std::string(" — ") + msg : std::string());
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
            p = best->construct(detail::complete(best->params, best->n_params, args));
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
        // ...and so are combobox choices, min / max, length, regex, requires_.
        try {
            if (std::string why = detail::check_field(*f, v, *this); !why.empty()) {
                return {Any::none(), why};
            }
        } catch (const std::exception &e) {
            return {Any::none(), std::string("checking ") + f->name + ": " + e.what()};
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
            const ArgList full = detail::complete(m->params, m->n_params, args);
            if (std::string why = detail::check_call(*m, *this, full, ref_.cls->name); !why.empty()) {
                return {Any::none(), why};
            }
            return {m->invoke(ref_, full), {}};
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
            const ArgList full = detail::complete(m->params, m->n_params, args);
            if (std::string why = detail::check_call(*m, Object{}, full, k.name); !why.empty()) {
                return {Any::none(), why};
            }
            return {m->invoke(ObjectRef{}, full), {}};
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
            return {f.invoke(ObjectRef{}, detail::complete(f.params, f.n_params, args)), {}};
        } catch (const std::exception &e) {
            return {Any::none(), std::string(f.qualified) + ": " + e.what()};
        }
    }

    inline const char *annotation(const MetaAnnotation *a, std::size_t n, std::string_view key) {
        for (std::size_t i = 0; i < n; ++i) {
            if (key == a[i].key) {
                return a[i].value;
            }
        }
        return nullptr;
    }

    inline Result evaluate(const Object &self, std::string_view expr, const MetaParam *params,
                           std::size_t n_params, const ArgList *args) {
        try {
            detail::Expr     e{expr, 0, &self, params, n_params, args, {}, nullptr};
            const detail::Val v = e.run();
            switch (v.k) {
            case detail::Val::boolean:
                return {Any::boolean(v.b), {}};
            case detail::Val::num:
                return {Any::real(v.d), {}};
            case detail::Val::str:
                return {Any::text(v.s), {}};
            }
        } catch (const std::exception &e) {
            return {Any::none(), e.what()};
        }
        return {Any::none(), "unreachable"};
    }

    inline bool condition(const Object &self, std::string_view expr) {
        const Result r = evaluate(self, expr);
        if (!r.ok()) {
            return true; // fail open: a broken hint must not lock a field
        }
        switch (r.value.kind()) {
        case Kind::boolean:
            return r.value.as_bool();
        case Kind::number:
            return r.value.as_number() != 0;
        case Kind::string:
            return !r.value.as_string().empty();
        default:
            return true;
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
