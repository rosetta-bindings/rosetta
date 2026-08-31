// Copyright (c) fmaerten@gmail.com
// License: MIT

// The dynamic interpreter, and the metadata queries a UI needs — host-agnostic.
//
// Two front-ends share this file verbatim: demo.cpp (a terminal session) and
// qt/viewer.cpp (a Qt window with a 3D view, a generated property panel and a
// console). Neither this header nor either front-end includes scene.h or names
// a single bound type. Everything they know, they ask the metadata.
//
// That is the claim under test. A conventional binding would need one generated
// wrapper per class per host; here the *host* is written once and the set of
// bound classes is data it discovers at startup.
//
// Stock C++20 — no reflection, no Qt.

#pragma once

#include <rosetta/dynamic.h>

#include <cctype>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace dynui {

    namespace rd = rosetta::dyn;

    // -----------------------------------------------------------------------
    // Reading the metadata
    // -----------------------------------------------------------------------

    /** @brief One lowered annotation (label / widget / button), or `fallback`. */
    inline std::string ann(const rd::MetaAnnotation *a, std::size_t n, std::string_view key,
                           std::string fallback = {}) {
        for (std::size_t i = 0; i < n; ++i) {
            if (key == a[i].key) {
                return a[i].value;
            }
        }
        return fallback;
    }

    inline std::string label_of(const rd::MetaField &f) {
        return ann(f.annotations, f.n_annotations, "label", f.name);
    }

    /**
     * @brief Which editor to draw for a field.
     *
     * An explicit `rosetta::widget::*` hint wins. Otherwise infer from the type:
     * choices mean a combo, a bounded float means a slider, and so on. Both
     * front-ends use this — the terminal one prints the name, the Qt one
     * constructs the matching QWidget.
     */
    inline std::string widget_for(const rd::MetaField &f) {
        const std::string hint = ann(f.annotations, f.n_annotations, "widget");
        if (!hint.empty()) {
            return hint;
        }
        if (f.n_choices || f.type->n_enumerators) {
            return "combo";
        }
        switch (f.type->kind) {
        case rd::Kind::boolean:
            return "checkbox";
        case rd::Kind::number:
            return f.range.has && !f.type->integral ? "slider" : "spin";
        case rd::Kind::vector:
            return "list";
        case rd::Kind::object:
            return "subform";
        default:
            return "textfield";
        }
    }

    /** @brief The choices a combo should offer: the `combobox` annotation for a
     *  string field, the enumerators for an enum one. Neither needed codegen. */
    inline std::vector<std::string> choices_for(const rd::MetaField &f) {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < f.n_choices; ++i) {
            out.emplace_back(f.choices[i]);
        }
        for (std::size_t i = 0; i < f.type->n_enumerators; ++i) {
            out.emplace_back(f.type->enumerators[i].name);
        }
        return out;
    }

    /**
     * @brief Resolve a token against the type it is destined for.
     *
     * Today that means one thing: a bare word standing for an enumerator
     * ("Wireframe") becomes that enumeration's value, looked up in the
     * TypeDesc. No enumeration is hard-coded here.
     *
     * This deliberately lives in the HOST, not in rosetta::dyn::match(). If the
     * core treated a string as convertible-to-enum, then `f(std::string)` and
     * `f(Shading)` would score identically for the token `Flat` and every such
     * call would report an ambiguity. Name resolution is a language-binding
     * concern; the core's job is to score types it was actually handed.
     */
    inline rd::Any coerce(const rd::TypeDesc *want, const rd::Any &v) {
        if (!want || want->kind != rd::Kind::enum_ || v.kind() != rd::Kind::string) {
            return v;
        }
        const std::string &s = v.as_string();
        for (std::size_t i = 0; i < want->n_enumerators; ++i) {
            if (s == want->enumerators[i].name) {
                return rd::Any::enumeration(want->enumerators[i].value, want);
            }
        }
        return v; // unknown name: hand it through so the error stays truthful
    }

    /** @brief The enumerator name for a value, for display. */
    inline std::string enumerator_name(const rd::TypeDesc *t, long long v) {
        for (std::size_t i = 0; t && i < t->n_enumerators; ++i) {
            if (t->enumerators[i].value == v) {
                return t->enumerators[i].name;
            }
        }
        return std::to_string(v);
    }

    /**
     * @brief Does this class speak the geometry protocol the 3D view draws?
     *
     * Duck typing, but *checked against the metadata*: the names are a
     * convention (`positions`, `triangles`), and the shapes — nullary, callable,
     * returning a sequence of numbers — are verified before anything is called.
     * This is how a dynamic object model grows an "interface": no base class, no
     * registration, and a library that happens to have the right surface just
     * works. Graphite's GOM does the same thing.
     */
    inline bool has_geometry(const rd::MetaClass &k) {
        const rd::MetaMethod *p = k.method("positions");
        const rd::MetaMethod *t = k.method("triangles");
        const auto            ok = [](const rd::MetaMethod *m) {
            return m && m->invoke && m->n_params == 0 && m->ret &&
                   m->ret->kind == rd::Kind::vector && m->ret->element &&
                   m->ret->element->kind == rd::Kind::number;
        };
        return ok(p) && ok(t);
    }

    // -----------------------------------------------------------------------
    // The interpreter
    // -----------------------------------------------------------------------

    /**
     * @brief A variable table plus a command parser, over any bound classes.
     *
     * There is nothing class-specific in here. `new`, `get`, `set`, `call` and
     * `static` are implemented once against rosetta::dyn, so the command set
     * does not grow when the bound library does.
     */
    class Interp {
    public:
        std::map<std::string, rd::Object> vars;

        /** @brief One line of output for the host to display. */
        std::function<void(const std::string &, bool is_error)> out =
            [](const std::string &, bool) {};
        /** @brief Something mutated — the host may want to redraw. */
        std::function<void()> changed = [] {};

        void run(const std::string &line);

        /** @brief Turn a bare token into an Any, the way a scripting host would.
         *
         *  Note what is NOT happening: no consulting the target signature. The
         *  token becomes the most specific thing it looks like, and
         *  rosetta::dyn::match() sorts out promotion and overload choice.
         *  `$name` refers to an object slot, which is how one bound class gets
         *  passed to another's method. */
        rd::Any parse_token(const std::string &t) const;

        /** @brief A fresh variable name with the given stem ("cube" -> "cube2"). */
        std::string fresh(const std::string &stem);

    private:
        void say(const std::string &s) { out(s, false); }
        void err(const std::string &s) { out(s, true); }
        void show(const rd::Result &r);
        bool store(const std::string &name, const rd::Result &r);
        rd::Object *var(const std::string &n);
    };

    // -----------------------------------------------------------------------

    inline rd::Any Interp::parse_token(const std::string &t) const {
        if (!t.empty() && t[0] == '$') {
            auto it = vars.find(t.substr(1));
            return it == vars.end() ? rd::Any::none() : it->second.as_any();
        }
        if (t == "true" || t == "false") {
            return rd::Any::boolean(t == "true");
        }
        if (!t.empty() &&
            (std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '-' || t[0] == '+')) {
            try {
                std::size_t used = 0;
                if (t.find('.') == std::string::npos && t.find('e') == std::string::npos) {
                    const long long v = std::stoll(t, &used);
                    if (used == t.size()) {
                        return rd::Any::integer(v);
                    }
                }
                const double d = std::stod(t, &used);
                if (used == t.size()) {
                    return rd::Any::real(d);
                }
            } catch (const std::exception &) {
                // not a number after all — fall through to string
            }
        }
        return rd::Any::text(t);
    }

    inline std::string Interp::fresh(const std::string &stem) {
        for (int i = 1;; ++i) {
            const std::string n = stem + (i == 1 ? "" : std::to_string(i));
            if (!vars.count(n)) {
                return n;
            }
        }
    }

    inline void Interp::show(const rd::Result &r) {
        if (!r.ok()) {
            err(r.error);
        } else if (r.value.kind() != rd::Kind::void_) {
            say("=> " + r.value.to_string());
        } else {
            say("ok");
        }
    }

    inline bool Interp::store(const std::string &name, const rd::Result &r) {
        if (!r.ok()) {
            err(r.error);
            return false;
        }
        const rd::ObjectRef &o = r.value.as_object();
        vars[name]             = rd::Object::adopt(*o.cls, o.ptr, o.owner);
        say(name + " = " + r.value.to_string());
        return true;
    }

    inline rd::Object *Interp::var(const std::string &n) {
        auto it = vars.find(n);
        if (it == vars.end()) {
            err("no such variable: " + n);
            return nullptr;
        }
        return &it->second;
    }

    inline void Interp::run(const std::string &line) {
        std::istringstream       in(line);
        std::vector<std::string> tok;
        for (std::string w; in >> w;) {
            tok.push_back(w);
        }
        if (tok.empty() || tok[0][0] == '#') {
            return;
        }
        const std::string &cmd = tok[0];

        // Build the argument list, resolving each token against the parameter
        // type when we can tell which one it is. `ps` is null when the callee is
        // still ambiguous, in which case the tokens go through untouched and
        // overload scoring decides.
        const auto args_from = [&](std::size_t from, const rd::MetaParam *ps, std::size_t np) {
            rd::ArgList a;
            for (std::size_t i = from; i < tok.size(); ++i) {
                rd::Any v  = parse_token(tok[i]);
                const std::size_t pi = i - from;
                a.add(ps && pi < np ? coerce(ps[pi].type, v) : v);
            }
            return a;
        };

        // The unique same-named method of the right arity, or null when the
        // overload set cannot be narrowed on arity alone.
        const auto sole_overload = [&](const rd::MetaClass &k, const std::string &name,
                                       std::size_t argc) -> const rd::MetaMethod * {
            const rd::MetaMethod *hit = nullptr;
            for (const rd::MetaMethod *m : k.overloads(name)) {
                if (m->n_params == argc) {
                    if (hit) {
                        return nullptr; // genuinely ambiguous on arity
                    }
                    hit = m;
                }
            }
            return hit;
        };

        if (cmd == "classes") {
            for (const rd::MetaClass *k : rd::registry().classes()) {
                say(std::string(k->qualified) + "  (" + std::to_string(k->n_fields) +
                    " fields, " + std::to_string(k->n_methods) + " methods, " +
                    std::to_string(k->n_ctors) + " ctors)" +
                    (has_geometry(*k) ? "  [drawable]" : ""));
            }
            for (const rd::MetaEnum *e : rd::registry().enums()) {
                say(std::string(e->qualified) + "  (enum, " + std::to_string(e->n_values) +
                    " values)");
            }
            return;
        }
        if (cmd == "vars") {
            for (const auto &[n, o] : vars) {
                say(n + " : " + o.as_any().to_string());
            }
            return;
        }
        if (cmd == "new" && tok.size() >= 3) {
            const rd::MetaClass *k = rd::registry().find_class(tok[2]);
            if (!k) {
                err("no such class: " + tok[2]);
                return;
            }
            // Same narrowing for constructors: a unique arity means the
            // parameter types are known, so enumerator names resolve there too.
            const rd::MetaCtor *only = nullptr;
            std::size_t         hits = 0;
            for (std::size_t i = 0; i < k->n_ctors; ++i) {
                if (k->ctors[i].n_params == tok.size() - 3) {
                    only = &k->ctors[i];
                    ++hits;
                }
            }
            if (hits != 1) {
                only = nullptr;
            }
            store(tok[1], rd::Object::create(
                              *k, args_from(3, only ? only->params : nullptr,
                                            only ? only->n_params : 0)));
            changed();
            return;
        }
        if (cmd == "methods" && tok.size() == 2) {
            const rd::MetaClass *k = rd::registry().find_class(tok[1]);
            if (!k) {
                if (rd::Object *o = var(tok[1])) {
                    k = o->meta();
                } else {
                    return;
                }
            }
            for (std::size_t i = 0; i < k->n_methods; ++i) {
                const rd::MetaMethod &m = k->methods[i];
                std::string           s = std::string(m.is_static ? "static " : "") +
                                m.ret->spelling + " " + m.name + "(";
                for (std::size_t j = 0; j < m.n_params; ++j) {
                    s += (j ? ", " : "") + std::string(m.params[j].type->spelling);
                }
                s += ")";
                if (m.overload_count > 1) {
                    s += "  [" + std::to_string(m.overload_index + 1) + "/" +
                         std::to_string(m.overload_count) + "]";
                }
                if (!m.invoke) {
                    s += "  -- unavailable: " + std::string(m.skip_reason);
                } else if (*m.doc) {
                    s += "  -- " + std::string(m.doc);
                }
                say(s);
            }
            return;
        }
        if (cmd == "get" && tok.size() == 3) {
            if (rd::Object *o = var(tok[1])) {
                show(o->get(tok[2]));
            }
            return;
        }
        if (cmd == "set" && tok.size() >= 4) {
            if (rd::Object *o = var(tok[1])) {
                // Join the tail, so `set m name my mesh` works.
                std::string v = tok[3];
                for (std::size_t i = 4; i < tok.size(); ++i) {
                    v += " " + tok[i];
                }
                // The field's type is known, so an enumerator name resolves.
                const rd::MetaField *f = o->meta() ? o->meta()->field(tok[2]) : nullptr;
                rd::Any              a = parse_token(v);
                show(o->set(tok[2], f ? coerce(f->type, a) : a));
                changed();
            }
            return;
        }
        if (cmd == "call" && tok.size() >= 3) {
            if (rd::Object *o = var(tok[1])) {
                const rd::MetaMethod *only =
                    o->meta() ? sole_overload(*o->meta(), tok[2], tok.size() - 3) : nullptr;
                const rd::Result r = o->call(
                    tok[2], args_from(3, only ? only->params : nullptr,
                                      only ? only->n_params : 0));
                // A method returning an object binds to `_`, so it can be used
                // by the next command.
                if (r.ok() && r.value.kind() == rd::Kind::object) {
                    store("_", r);
                } else {
                    show(r);
                }
                changed();
            }
            return;
        }
        if (cmd == "static" && tok.size() >= 4) {
            const rd::MetaClass *k = rd::registry().find_class(tok[2]);
            if (!k) {
                err("no such class: " + tok[2]);
                return;
            }
            const rd::MetaMethod *only = sole_overload(*k, tok[3], tok.size() - 4);
            const rd::Result      r    = rd::call_static(
                *k, tok[3],
                args_from(4, only ? only->params : nullptr, only ? only->n_params : 0));
            if (r.ok() && r.value.kind() == rd::Kind::object) {
                store(tok[1], r);
            } else {
                show(r);
            }
            changed();
            return;
        }
        if (cmd == "del" && tok.size() == 2) {
            vars.erase(tok[1]);
            changed();
            return;
        }
        err("usage: classes | vars | new <var> <Class> [args] | methods <Class|var> | "
            "get <var> <field> | set <var> <field> <v> | call <var> <method> [args] | "
            "static <var> <Class> <method> [args] | del <var>");
    }

} // namespace dynui
