// Copyright (c) fmaerten@gmail.com
// License: MIT

// Bodies for <rosetta/runtime/mcp.h>. See that header for the design.

#pragma once

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <istream>
#include <limits>
#include <ostream>
#include <streambuf>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace rosetta::mcp {

    namespace detail {

        // Protocol revisions this server speaks. The tool surface is the same
        // in all of them; a client asking for one we know gets it echoed back,
        // anything else gets the newest.
        inline constexpr const char *k_protocols[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

        inline json rpc_result(const json &id, json result) {
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
        }

        inline json rpc_error(const json &id, int code, const std::string &message) {
            return {{"jsonrpc", "2.0"},
                    {"id", id},
                    {"error", {{"code", code}, {"message", message}}}};
        }

        /** @brief Thrown for a JSON-RPC protocol error (as opposed to a tool error). */
        struct RpcError {
            int         code;
            std::string message;
        };

        inline std::string str_arg(const json &a, const char *key) {
            if (!a.is_object() || !a.contains(key) || !a[key].is_string()) {
                throw dyn::Error(std::string("missing string argument '") + key + "'");
            }
            return a[key].get<std::string>();
        }

        inline std::string ownership_of(const dyn::ObjectRef &r) {
            if (!r.owner) {
                return "borrowed";
            }
            return r.owner.get() == r.ptr ? "owned" : "pinned";
        }

        /** @brief "Mesh" out of "scene::Mesh", for handle names. */
        inline std::string short_name(const dyn::MetaClass &k) {
            std::string_view n = *k.name ? k.name : k.qualified;
            const auto       p = n.rfind("::");
            return std::string(p == std::string_view::npos ? n : n.substr(p + 2));
        }

        inline json type_json(const dyn::TypeDesc *t) {
            if (!t) {
                return "unknown";
            }
            json j = {{"type", t->spelling}, {"kind", dyn::kind_name(t->kind)}};
            if (t->kind == dyn::Kind::enum_ && t->n_enumerators) {
                json v = json::array();
                for (std::size_t i = 0; i < t->n_enumerators; ++i) {
                    v.push_back(t->enumerators[i].name);
                }
                j["values"] = v;
            }
            if (t->kind == dyn::Kind::object) {
                j["class"]       = t->object;
                j["pass_handle"] = true;
            }
            if (t->kind == dyn::Kind::vector && t->element) {
                j["element"] = type_json(t->element);
            }
            return j;
        }

        inline json params_json(const dyn::MetaParam *ps, std::size_t n) {
            json out = json::array();
            for (std::size_t i = 0; i < n; ++i) {
                if (ps[i].is_out) {
                    continue; // filled by the callee; joins the return value
                }
                json p    = type_json(ps[i].type);
                p["name"] = ps[i].name;
                out.push_back(std::move(p));
            }
            return out;
        }

        inline std::string signature(const char *name, const dyn::MetaParam *ps, std::size_t n,
                                     const dyn::TypeDesc *ret) {
            std::string s = std::string(name) + "(";
            bool        first = true;
            for (std::size_t i = 0; i < n; ++i) {
                if (ps[i].is_out) {
                    continue;
                }
                s += first ? "" : ", ";
                s += std::string(ps[i].type ? ps[i].type->spelling : "?") + " " + ps[i].name;
                first = false;
            }
            s += ")";
            if (ret) {
                s += " -> " + std::string(ret->spelling);
            }
            return s;
        }

        // Fields and methods visible on a class, inherited ones included, a
        // derived declaration shadowing a base one of the same name (the rule
        // MetaClass::field / overloads already apply on lookup).
        inline void collect_fields(const dyn::MetaClass &k, std::vector<const dyn::MetaField *> &out) {
            for (std::size_t i = 0; i < k.n_fields; ++i) {
                const bool seen = std::any_of(out.begin(), out.end(), [&](const dyn::MetaField *f) {
                    return std::string_view(f->name) == k.fields[i].name;
                });
                if (!seen) {
                    out.push_back(&k.fields[i]);
                }
            }
            for (std::size_t b = 0; b < k.n_bases; ++b) {
                collect_fields(*k.bases[b], out);
            }
        }

        inline void collect_methods(const dyn::MetaClass &k,
                                    std::vector<const dyn::MetaMethod *> &out) {
            for (std::size_t i = 0; i < k.n_methods; ++i) {
                out.push_back(&k.methods[i]);
            }
            for (std::size_t b = 0; b < k.n_bases; ++b) {
                std::vector<const dyn::MetaMethod *> base;
                collect_methods(*k.bases[b], base);
                for (const dyn::MetaMethod *m : base) {
                    const bool hidden = std::any_of(out.begin(), out.end(), [&](auto *o) {
                        return std::string_view(o->name) == m->name;
                    });
                    if (!hidden) {
                        out.push_back(m);
                    }
                }
            }
        }

        /** @brief A minimal streambuf over a raw file descriptor (write-only). */
        class FdOut : public std::streambuf {
        public:
            explicit FdOut(int fd) : fd_(fd) {}

        protected:
            int_type overflow(int_type c) override {
                if (c != traits_type::eof()) {
                    const char ch = static_cast<char>(c);
                    if (write_all(&ch, 1) != 1) {
                        return traits_type::eof();
                    }
                }
                return c;
            }
            std::streamsize xsputn(const char *s, std::streamsize n) override {
                return write_all(s, n);
            }

        private:
            std::streamsize write_all(const char *s, std::streamsize n) {
                std::streamsize done = 0;
                while (done < n) {
#if defined(_WIN32)
                    const auto w = ::_write(fd_, s + done, static_cast<unsigned>(n - done));
#else
                    const auto w = ::write(fd_, s + done, static_cast<std::size_t>(n - done));
#endif
                    if (w <= 0) {
                        break;
                    }
                    done += w;
                }
                return done;
            }
            int fd_;
        };

    } // namespace detail

    // ---------------------------------------------------------------------
    // Construction, handle table
    // ---------------------------------------------------------------------

    inline Server::Server(Options opts, dyn::Registry &reg) : opts_(std::move(opts)), reg_(reg) {}

    inline const dyn::Object *Server::find(std::string_view handle) const {
        auto it = objects_.find(std::string(handle));
        return it == objects_.end() ? nullptr : &it->second;
    }

    inline std::string Server::store(dyn::Object obj, const std::string &wanted) {
        std::string h = wanted;
        if (h.empty()) {
            const std::string base = detail::short_name(*obj.meta());
            do {
                h = base + "#" + std::to_string(++counters_[base]);
            } while (objects_.count(h));
        }
        objects_[h] = std::move(obj);
        return h;
    }

    inline const dyn::Object &Server::object_arg(const json &a, const char *key) const {
        std::string h;
        if (a.is_object() && a.contains(key) && a[key].is_object() && a[key].contains("$ref")) {
            h = a[key]["$ref"].get<std::string>();
        } else {
            h = detail::str_arg(a, key);
        }
        const dyn::Object *o = find(h);
        if (!o) {
            throw dyn::Error("no object named '" + h + "' — list_objects shows the live handles");
        }
        return *o;
    }

    inline void Server::add_tool(json definition, ToolFn fn) {
        extra_tools_.emplace_back(std::move(definition), std::move(fn));
    }

    inline std::vector<Server::Signature> Server::ctor_signatures(const dyn::MetaClass &k) {
        std::vector<Signature> out;
        for (std::size_t i = 0; i < k.n_ctors; ++i) {
            out.push_back({k.ctors[i].params, k.ctors[i].n_params});
        }
        return out;
    }

    inline std::vector<Server::Signature> Server::method_signatures(const dyn::MetaClass &k,
                                                                    std::string_view method) {
        std::vector<Signature> out;
        for (const dyn::MetaMethod *m : k.overloads(method)) {
            out.push_back({m->params, m->n_params});
        }
        return out;
    }

    // ---------------------------------------------------------------------
    // Marshalling
    // ---------------------------------------------------------------------

    inline dyn::Any Server::from_json(const json &v) const {
        using dyn::Any;

        // A handle reference.
        if (v.is_object() && v.contains("$ref") && v["$ref"].is_string()) {
            const std::string h = v["$ref"].get<std::string>();
            const dyn::Object *o = find(h);
            if (!o) {
                throw dyn::Error("no object named '" + h + "'");
            }
            return o->as_any();
        }
        if (v.is_null()) {
            return Any::none();
        }
        if (v.is_boolean()) {
            return Any::boolean(v.get<bool>());
        }
        if (v.is_number_integer() || v.is_number_unsigned()) {
            return Any::integer(v.get<long long>());
        }
        if (v.is_number()) {
            return Any::real(v.get<double>());
        }
        if (v.is_string()) {
            return Any::text(v.get<std::string>());
        }
        if (v.is_array()) {
            std::vector<Any> out;
            out.reserve(v.size());
            for (const json &e : v) {
                out.push_back(from_json(e));
            }
            return Any::list(std::move(out));
        }
        throw dyn::Error("cannot pass " + v.dump() +
                         " — objects are passed as {\"$ref\": \"<handle>\"}");
    }

    inline dyn::Any Server::coerce(dyn::Any v, const dyn::TypeDesc *want) const {
        using dyn::Any;
        using dyn::Kind;
        if (!want) {
            return v;
        }
        switch (want->kind) {
        case Kind::enum_:
            if (v.kind() == Kind::string) {
                const std::string &s = v.as_string();
                for (std::size_t i = 0; i < want->n_enumerators; ++i) {
                    if (s == want->enumerators[i].name) {
                        return Any::enumeration(want->enumerators[i].value, want);
                    }
                }
                std::string names;
                for (std::size_t i = 0; i < want->n_enumerators; ++i) {
                    names += (i ? ", " : "") + std::string(want->enumerators[i].name);
                }
                throw dyn::Error("'" + s + "' is not a " + want->spelling + " (one of: " + names +
                                 ")");
            }
            if (v.kind() == Kind::number && v.type() && v.type()->integral) {
                return Any::enumeration(v.as_int(), want);
            }
            break;
        case Kind::object:
            // A bare handle name where an object is expected.
            if (v.kind() == Kind::string) {
                if (const dyn::Object *o = find(v.as_string())) {
                    return o->as_any();
                }
                throw dyn::Error("expected a handle to a " + std::string(want->spelling) +
                                 ", and no object is named '" + v.as_string() + "'");
            }
            break;
        case Kind::number:
            // 2 for a double parameter: keep the declared type so the value is
            // exact rather than merely promotable.
            if (v.kind() == Kind::number && !want->integral) {
                return Any::real(v.as_number(), want);
            }
            break;
        case Kind::vector:
            if (v.kind() == Kind::vector) {
                std::vector<Any> out;
                out.reserve(v.as_list().size());
                for (const Any &e : v.as_list()) {
                    out.push_back(coerce(e, want->element));
                }
                return Any::list(std::move(out), want);
            }
            break;
        default:
            break;
        }
        return v;
    }

    inline dyn::ArgList Server::coerce_args(std::vector<dyn::Any>        raw,
                                            const std::vector<Signature> &cands) const {
        // Narrow to the signatures of the right arity; at each position, if
        // they all agree on the parameter type, coerce against it. Where they
        // disagree the value stays as given and resolve() picks the overload.
        std::vector<const Signature *> fit;
        for (const Signature &s : cands) {
            if (dyn::detail::input_arity(s.params, s.n_params) == raw.size()) {
                fit.push_back(&s);
            }
        }
        auto param_at = [](const Signature &s, std::size_t pos) -> const dyn::TypeDesc * {
            for (std::size_t i = 0, k = 0; i < s.n_params; ++i) {
                if (s.params[i].is_out) {
                    continue;
                }
                if (k++ == pos) {
                    return s.params[i].type;
                }
            }
            return nullptr;
        };
        dyn::ArgList out;
        for (std::size_t pos = 0; pos < raw.size(); ++pos) {
            const dyn::TypeDesc *want = nullptr;
            if (!fit.empty()) {
                want = param_at(*fit.front(), pos);
                for (const Signature *s : fit) {
                    if (!dyn::same_type(param_at(*s, pos), want)) {
                        want = nullptr;
                        break;
                    }
                }
            }
            out.add(coerce(std::move(raw[pos]), want));
        }
        return out;
    }

    inline json Server::encode_list(std::vector<json> items) const {
        if (items.size() <= opts_.max_list_items) {
            return json(std::move(items));
        }
        // Too big to be useful to an agent: describe it instead.
        json head = json::array();
        for (std::size_t i = 0; i < std::min(opts_.summary_head, items.size()); ++i) {
            head.push_back(items[i]);
        }
        json   s  = {{"$summary", true}, {"length", items.size()}, {"head", head}};
        double lo = std::numeric_limits<double>::infinity(), hi = -lo, sum = 0;
        for (const json &e : items) {
            if (!e.is_number()) {
                return s;
            }
            const double d = e.get<double>();
            lo             = std::min(lo, d);
            hi             = std::max(hi, d);
            sum += d;
        }
        s["min"]  = lo;
        s["max"]  = hi;
        s["mean"] = sum / static_cast<double>(items.size());
        return s;
    }

    inline json Server::encode(const dyn::Any &v) {
        using dyn::Kind;
        if (v.empty()) {
            return nullptr;
        }
        switch (v.kind()) {
        case Kind::void_:
            return nullptr;
        case Kind::boolean:
            return v.as_bool();
        case Kind::number:
            if (v.type() && v.type()->integral) {
                return v.as_int();
            }
            return v.as_number();
        case Kind::string:
            return v.as_string();
        case Kind::enum_: {
            const long long      x = v.as_int();
            const dyn::TypeDesc *t = v.type();
            for (std::size_t i = 0; t && i < t->n_enumerators; ++i) {
                if (t->enumerators[i].value == x) {
                    return t->enumerators[i].name;
                }
            }
            return x;
        }
        case Kind::vector: {
            const auto &l = v.as_list();
            // Only the head of a long sequence survives the summary, so only
            // encode what can be shown — otherwise a vector of objects would
            // mint a handle for every element.
            std::vector<json> items;
            items.reserve(l.size());
            const bool        summarize = l.size() > opts_.max_list_items;
            for (std::size_t i = 0; i < l.size(); ++i) {
                const bool scalar = l[i].kind() == Kind::number;
                items.push_back(!summarize || scalar || i < opts_.summary_head ? encode(l[i])
                                                                               : json());
            }
            return encode_list(std::move(items));
        }
        case Kind::object: {
            const dyn::ObjectRef &r = v.as_object();
            if (!r.ptr || !r.cls) {
                return nullptr;
            }
            // The same sub-object reached twice (a T& getter called again)
            // keeps its handle rather than minting a new one each time.
            for (const auto &[h, o] : objects_) {
                if (o.ptr() == r.ptr && o.meta() == r.cls) {
                    return {{"$ref", h},
                            {"class", r.cls->qualified},
                            {"ownership", detail::ownership_of(o.ref())}};
                }
            }
            const std::string h = store(dyn::Object::adopt(*r.cls, r.ptr, r.owner));
            return {{"$ref", h},
                    {"class", r.cls->qualified},
                    {"ownership", detail::ownership_of(r)}};
        }
        case Kind::unknown:
            break;
        }
        return {{"$opaque", v.type() ? v.type()->spelling : "?"}};
    }

    inline dyn::ArgList Server::args_for(const json &args, const std::vector<Signature> &cands) const {
        if (args.is_null()) {
            return {};
        }

        // Keyword form: {"m": {"$ref": "Mesh#1"}}. Pick the one signature
        // whose parameter names are exactly the keys given, then lay the
        // values out positionally.
        if (args.is_object()) {
            const Signature *hit = nullptr;
            for (const Signature &s : cands) {
                if (dyn::detail::input_arity(s.params, s.n_params) != args.size()) {
                    continue;
                }
                bool all = true;
                for (std::size_t i = 0; i < s.n_params && all; ++i) {
                    all = s.params[i].is_out || args.contains(s.params[i].name);
                }
                if (all) {
                    if (hit) {
                        throw dyn::Error("named arguments match more than one overload — "
                                         "pass them positionally as an array");
                    }
                    hit = &s;
                }
            }
            if (!hit) {
                std::string sigs;
                for (const Signature &s : cands) {
                    sigs += "\n  " + detail::signature("", s.params, s.n_params, nullptr);
                }
                throw dyn::Error("no overload takes exactly the arguments " + args.dump() +
                                 "; candidates:" + (sigs.empty() ? " none" : sigs));
            }
            dyn::ArgList out;
            for (std::size_t i = 0; i < hit->n_params; ++i) {
                if (!hit->params[i].is_out) {
                    out.add(hit->params[i].name,
                            coerce(from_json(args[hit->params[i].name]), hit->params[i].type));
                }
            }
            return out;
        }

        if (!args.is_array()) {
            throw dyn::Error("'args' must be an array (positional) or an object (by name)");
        }
        std::vector<dyn::Any> raw;
        raw.reserve(args.size());
        for (const json &a : args) {
            raw.push_back(from_json(a));
        }
        return coerce_args(std::move(raw), cands);
    }

    // ---------------------------------------------------------------------
    // Tools
    // ---------------------------------------------------------------------

    inline json Server::list_classes() const {
        json classes = json::array();
        for (const dyn::MetaClass *k : reg_.classes()) {
            json factories = json::array();
            for (std::size_t i = 0; i < k->n_methods; ++i) {
                const dyn::MetaMethod &m = k->methods[i];
                if (m.is_static && m.invoke && m.ret && m.ret->kind == dyn::Kind::object) {
                    factories.push_back(m.name);
                }
            }
            json c = {{"name", k->name},
                      {"qualified", k->qualified},
                      {"creatable", !k->is_abstract && k->destroy && k->n_ctors > 0}};
            if (*k->doc) {
                c["doc"] = k->doc;
            }
            if (!factories.empty()) {
                c["static_factories"] = factories;
            }
            classes.push_back(std::move(c));
        }

        json enums = json::array();
        for (const dyn::MetaEnum *e : reg_.enums()) {
            json v = json::array();
            for (std::size_t i = 0; i < e->n_values; ++i) {
                v.push_back(e->values[i].name);
            }
            enums.push_back({{"name", e->qualified}, {"values", v}});
        }

        json out = {{"classes", classes}, {"enums", enums}};
        if (!reg_.functions().empty()) {
            json fns = json::array();
            for (const dyn::MetaFunction *f : reg_.functions()) {
                json j = {{"name", f->qualified},
                          {"signature", detail::signature(f->name, f->params, f->n_params, f->ret)}};
                if (*f->doc) {
                    j["doc"] = f->doc;
                }
                fns.push_back(std::move(j));
            }
            out["functions"] = fns;
        }
        return out;
    }

    inline json Server::describe_class(const json &a) const {
        const std::string     name = detail::str_arg(a, "class");
        const dyn::MetaClass *k    = reg_.find_class(name);
        if (!k) {
            throw dyn::Error("no class '" + name + "' — list_classes shows what is bound");
        }

        json bases = json::array();
        for (std::size_t i = 0; i < k->n_bases; ++i) {
            bases.push_back(k->bases[i]->qualified);
        }

        json ctors = json::array();
        for (std::size_t i = 0; i < k->n_ctors; ++i) {
            const dyn::MetaCtor &c = k->ctors[i];
            json j = {{"params", detail::params_json(c.params, c.n_params)}};
            if (*c.doc) {
                j["doc"] = c.doc;
            }
            ctors.push_back(std::move(j));
        }

        std::vector<const dyn::MetaField *> fs;
        detail::collect_fields(*k, fs);
        json fields = json::array();
        for (const dyn::MetaField *f : fs) {
            json j    = detail::type_json(f->type);
            j["name"] = f->name;
            if (*f->doc) {
                j["doc"] = f->doc;
            }
            if (f->readonly || !f->set) {
                j["readonly"] = true;
            }
            if (!f->get) {
                j["readable"] = false;
            }
            if (f->range.has) {
                j["range"] = {f->range.lo, f->range.hi};
            }
            if (f->n_choices) {
                json ch = json::array();
                for (std::size_t i = 0; i < f->n_choices; ++i) {
                    ch.push_back(f->choices[i]);
                }
                j["choices"] = ch;
            }
            fields.push_back(std::move(j));
        }

        std::vector<const dyn::MetaMethod *> ms;
        detail::collect_methods(*k, ms);
        json methods = json::array();
        for (const dyn::MetaMethod *m : ms) {
            json j = {{"name", m->name},
                      {"signature", detail::signature(m->name, m->params, m->n_params, m->ret)},
                      {"params", detail::params_json(m->params, m->n_params)},
                      {"returns", detail::type_json(m->ret)}};
            if (*m->doc) {
                j["doc"] = m->doc;
            }
            if (m->is_static) {
                j["static"] = true;
            }
            if (m->is_const) {
                j["const"] = true;
            }
            if (!m->invoke) {
                j["callable"]    = false;
                j["skip_reason"] = m->skip_reason;
            }
            methods.push_back(std::move(j));
        }

        json out = {{"name", k->name},
                    {"qualified", k->qualified},
                    {"creatable", !k->is_abstract && k->destroy && k->n_ctors > 0},
                    {"constructors", ctors},
                    {"fields", fields},
                    {"methods", methods}};
        if (*k->doc) {
            out["doc"] = k->doc;
        }
        if (!bases.empty()) {
            out["bases"] = bases;
        }
        if (k->is_abstract) {
            out["abstract"] = true;
        }
        return out;
    }

    inline json Server::create_object(const json &a) {
        const std::string     name = detail::str_arg(a, "class");
        const dyn::MetaClass *k    = reg_.find_class(name);
        if (!k) {
            throw dyn::Error("no class '" + name + "' — list_classes shows what is bound");
        }
        const dyn::ArgList args = args_for(a.value("args", json()), ctor_signatures(*k));

        dyn::Result r = dyn::Object::create(*k, args);
        if (!r.ok()) {
            throw dyn::Error(r.error);
        }
        const dyn::ObjectRef &ref = r.value.as_object();
        const std::string     h =
            store(dyn::Object::adopt(*k, ref.ptr, ref.owner), a.value("name", std::string()));

        json out = inspect_object({{"object", h}});
        return out;
    }

    inline json Server::call_method(const json &a) {
        const std::string target = detail::str_arg(a, "target");
        const std::string method = detail::str_arg(a, "method");

        const dyn::Object    *obj = find(target);
        const dyn::MetaClass *k   = obj ? obj->meta() : reg_.find_class(target);
        if (!k) {
            throw dyn::Error("'" + target +
                             "' is neither a live object (list_objects) nor a class (list_classes)");
        }

        const std::vector<Signature> cands = method_signatures(*k, method);
        if (cands.empty()) {
            throw dyn::Error(std::string(k->name) + " has no method '" + method +
                             "' — describe_class lists them");
        }
        const dyn::ArgList args = args_for(a.value("args", json()), cands);

        dyn::Result r = obj ? obj->call(method, args) : dyn::call_static(*k, method, args);
        if (!r.ok()) {
            throw dyn::Error(r.error);
        }

        // store_as names a returned object — `bunny` rather than `Mesh#4`.
        const std::string as = a.value("store_as", std::string());
        if (!as.empty() && r.value.kind() == dyn::Kind::object && r.value.as_object().ptr) {
            const dyn::ObjectRef &ref = r.value.as_object();
            store(dyn::Object::adopt(*ref.cls, ref.ptr, ref.owner), as);
            return {{"result", {{"$ref", as}, {"class", ref.cls->qualified}}}};
        }
        return {{"result", encode(r.value)}};
    }

    inline json Server::inspect_object(const json &a) {
        const dyn::Object &o = object_arg(a, "object");
        std::vector<const dyn::MetaField *> fs;
        detail::collect_fields(*o.meta(), fs);

        json fields = json::object();
        for (const dyn::MetaField *f : fs) {
            if (!f->get) {
                continue;
            }
            dyn::Result r = o.get(f->name);
            fields[f->name] = r.ok() ? encode(r.value) : json{{"$error", r.error}};
        }

        std::string h;
        for (const auto &[name, obj] : objects_) {
            if (&obj == &o) {
                h = name;
            }
        }
        return {{"handle", h},
                {"class", o.meta()->qualified},
                {"ownership", detail::ownership_of(o.ref())},
                {"fields", fields}};
    }

    inline json Server::set_fields(const json &a) {
        const dyn::Object &o = object_arg(a, "object");
        if (!a.contains("values") || !a["values"].is_object()) {
            throw dyn::Error("'values' must be an object of {field: value}");
        }

        json        updated = json::array();
        std::string errors;
        for (const auto &[name, v] : a["values"].items()) {
            const dyn::MetaField *f = o.meta()->field(name);
            try {
                const dyn::Result r = o.set(name, coerce(from_json(v), f ? f->type : nullptr));
                if (r.ok()) {
                    updated.push_back(name);
                } else {
                    errors += "\n  " + r.error;
                }
            } catch (const dyn::Error &e) {
                errors += "\n  " + name + ": " + e.what();
            }
        }
        if (!errors.empty()) {
            throw dyn::Error("updated " + updated.dump() + "; rejected:" + errors);
        }
        return {{"updated", updated}};
    }

    inline json Server::list_objects() {
        json out = json::array();
        for (const auto &[h, o] : objects_) {
            out.push_back({{"handle", h},
                           {"class", o.meta()->qualified},
                           {"ownership", detail::ownership_of(o.ref())}});
        }
        return {{"objects", out}};
    }

    inline json Server::release_object(const json &a) {
        const std::string h = detail::str_arg(a, "object");
        if (!objects_.erase(h)) {
            throw dyn::Error("no object named '" + h + "'");
        }
        return {{"released", h}};
    }

    inline json Server::call_function(const json &a) {
        const std::string name = detail::str_arg(a, "function");
        const auto        fns  = reg_.function_overloads(name);
        if (fns.empty()) {
            throw dyn::Error("no function '" + name + "' — list_classes lists the functions");
        }
        std::vector<Signature> cands;
        for (const dyn::MetaFunction *f : fns) {
            cands.push_back({f->params, f->n_params});
        }
        const dyn::ArgList args = args_for(a.value("args", json()), cands);

        // Free functions have no resolve(): try each overload, keep the first
        // that accepts the arguments, report every refusal otherwise.
        std::string why;
        for (const dyn::MetaFunction *f : fns) {
            dyn::Result r = dyn::call_function(*f, args);
            if (r.ok()) {
                return {{"result", encode(r.value)}};
            }
            why += "\n" + r.error;
        }
        throw dyn::Error(why.substr(1));
    }

    // ---------------------------------------------------------------------
    // Tool catalogue and dispatch
    // ---------------------------------------------------------------------

    inline json Server::tools() const {
        const json args_schema = {
            {"type", json::array({"array", "object"})},
            {"description",
             "Arguments, positionally as an array or by parameter name as an object. Objects "
             "are passed by handle: {\"$ref\": \"Mesh#1\"} (or the bare handle string where "
             "the parameter is a class). Enums are passed by enumerator name."}};
        auto obj = [](json props, std::vector<std::string> req) {
            return json{{"type", "object"}, {"properties", std::move(props)}, {"required", req}};
        };
        auto str = [](const char *d) { return json{{"type", "string"}, {"description", d}}; };

        json t = json::array();
        t.push_back({{"name", "list_classes"},
                     {"description",
                      "List every bound C++ class, enum and free function. Start here."},
                     {"inputSchema", obj(json::object(), {})}});
        t.push_back({{"name", "describe_class"},
                     {"description",
                      "Constructors, fields (type, doc, range, read-only) and methods "
                      "(signature, doc) of one class, inherited members included."},
                     {"inputSchema", obj({{"class", str("Class name, e.g. Mesh or scene::Mesh")}},
                                         {"class"})}});
        t.push_back(
            {{"name", "create_object"},
             {"description",
              "Construct a C++ object and keep it alive under a handle. Returns the handle "
              "and the new object's fields. The best-matching constructor is chosen from "
              "the arguments."},
             {"inputSchema",
              obj({{"class", str("Class to construct")},
                   {"args", args_schema},
                   {"name", str("Optional handle name; defaults to <Class>#<n>")}},
                  {"class"})}});
        t.push_back(
            {{"name", "call_method"},
             {"description",
              "Call a method on a live object, or a static method when `target` is a class "
              "name. Overloads are resolved from the arguments. A returned object becomes a "
              "new handle; large arrays come back summarized (length, head, min, max, mean)."},
             {"inputSchema",
              obj({{"target", str("Object handle, or a class name for a static method")},
                   {"method", str("Method name")},
                   {"args", args_schema},
                   {"store_as", str("Optional handle name for a returned object")}},
                  {"target", "method"})}});
        t.push_back({{"name", "inspect_object"},
                     {"description", "Read every field of a live object."},
                     {"inputSchema", obj({{"object", str("Object handle")}}, {"object"})}});
        t.push_back(
            {{"name", "set_fields"},
             {"description",
              "Assign one or more fields of a live object. Range and read-only constraints "
              "are enforced; a rejected value is reported and leaves that field unchanged."},
             {"inputSchema",
              obj({{"object", str("Object handle")},
                   {"values", {{"type", "object"}, {"description", "{field: value, ...}"}}}},
                  {"object", "values"})}});
        t.push_back({{"name", "list_objects"},
                     {"description", "The live object handles and their classes."},
                     {"inputSchema", obj(json::object(), {})}});
        t.push_back({{"name", "release_object"},
                     {"description", "Drop a handle; the object is destroyed when nothing "
                                     "else keeps it alive."},
                     {"inputSchema", obj({{"object", str("Object handle")}}, {"object"})}});
        if (!reg_.functions().empty()) {
            t.push_back({{"name", "call_function"},
                         {"description", "Call a bound free (non-member) function."},
                         {"inputSchema", obj({{"function", str("Function name")},
                                              {"args", args_schema}},
                                             {"function"})}});
        }
        for (const auto &[def, fn] : extra_tools_) {
            t.push_back(def);
        }
        return t;
    }

    inline json Server::call_tool(std::string_view name, const json &args) {
        json out;
        try {
            if (name == "list_classes") {
                out = list_classes();
            } else if (name == "describe_class") {
                out = describe_class(args);
            } else if (name == "create_object") {
                out = create_object(args);
            } else if (name == "call_method") {
                out = call_method(args);
            } else if (name == "inspect_object") {
                out = inspect_object(args);
            } else if (name == "set_fields") {
                out = set_fields(args);
            } else if (name == "list_objects") {
                out = list_objects();
            } else if (name == "release_object") {
                out = release_object(args);
            } else if (name == "call_function" && !reg_.functions().empty()) {
                out = call_function(args);
            } else if (auto it = std::find_if(extra_tools_.begin(), extra_tools_.end(),
                                              [&](const auto &t) { return t.first["name"] == name; });
                       it != extra_tools_.end()) {
                out = it->second(args);
            } else {
                throw detail::RpcError{-32602, "unknown tool: " + std::string(name)};
            }
        } catch (const detail::RpcError &) {
            throw;
        } catch (const std::exception &e) {
            // A TOOL error, not a protocol error: the agent sees the message
            // and can correct the call.
            return {{"content", json::array({{{"type", "text"}, {"text", e.what()}}})},
                    {"isError", true}};
        }
        return {{"content", json::array({{{"type", "text"}, {"text", out.dump(2)}}})},
                {"structuredContent", out},
                {"isError", false}};
    }

    inline std::string Server::instructions() const {
        std::string s =
            "This server exposes a C++ library through rosetta's dynamic object model. "
            "Call list_classes, then describe_class to learn a class's constructors, fields "
            "and methods. create_object (or a static factory via call_method) gives you a "
            "handle such as \"Mesh#1\"; pass it to call_method / inspect_object / set_fields, "
            "or as an argument to another method as {\"$ref\": \"Mesh#1\"}. Objects live until "
            "release_object. Field ranges and read-only flags are enforced by the server; "
            "errors explain what was wrong. Large arrays are summarized, not returned in full.";
        if (!opts_.instructions.empty()) {
            s += "\n\n" + opts_.instructions;
        }
        return s;
    }

    inline std::optional<json> Server::handle(const json &msg) {
        if (!msg.is_object()) {
            return detail::rpc_error(nullptr, -32600, "expected a JSON-RPC request object");
        }
        const bool  has_id = msg.contains("id");
        const json  id     = has_id ? msg["id"] : json();
        if (!msg.contains("method") || !msg["method"].is_string()) {
            // A response to something we sent (we send nothing) or junk.
            return has_id ? std::optional<json>(detail::rpc_error(id, -32600, "missing method"))
                          : std::nullopt;
        }
        const std::string method = msg["method"].get<std::string>();
        const json        params = msg.value("params", json::object());

        if (!has_id) {
            return std::nullopt; // notifications/initialized, notifications/cancelled, ...
        }

        try {
            if (method == "initialize") {
                std::string version = detail::k_protocols[0];
                const std::string asked = params.value("protocolVersion", std::string());
                for (const char *p : detail::k_protocols) {
                    if (asked == p) {
                        version = p;
                    }
                }
                return detail::rpc_result(
                    id, {{"protocolVersion", version},
                         {"capabilities", {{"tools", {{"listChanged", false}}}}},
                         {"serverInfo", {{"name", opts_.name}, {"version", opts_.version}}},
                         {"instructions", instructions()}});
            }
            if (method == "ping") {
                return detail::rpc_result(id, json::object());
            }
            if (method == "tools/list") {
                return detail::rpc_result(id, {{"tools", tools()}});
            }
            if (method == "tools/call") {
                if (!params.contains("name") || !params["name"].is_string()) {
                    return detail::rpc_error(id, -32602, "tools/call needs a tool name");
                }
                return detail::rpc_result(
                    id, call_tool(params["name"].get<std::string>(),
                                  params.value("arguments", json::object())));
            }
            return detail::rpc_error(id, -32601, "method not found: " + method);
        } catch (const detail::RpcError &e) {
            return detail::rpc_error(id, e.code, e.message);
        } catch (const std::exception &e) {
            return detail::rpc_error(id, -32603, e.what());
        }
    }

    // ---------------------------------------------------------------------
    // Transport
    // ---------------------------------------------------------------------

    inline int Server::serve(std::istream &in, std::ostream &out) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.find_first_not_of(" \t\r") == std::string::npos) {
                continue;
            }
            std::optional<json> reply;
            try {
                reply = handle(json::parse(line));
            } catch (const json::parse_error &e) {
                reply = detail::rpc_error(nullptr, -32700, std::string("parse error: ") + e.what());
            }
            if (reply) {
                out << reply->dump() << '\n';
                out.flush();
            }
        }
        return 0;
    }

    inline int Server::serve_stdio() {
        // Keep the real stdout for the protocol, and point fd 1 at stderr so
        // anything the bound library prints lands in the client's log rather
        // than in the middle of a JSON-RPC message.
        std::fflush(stdout);
#if defined(_WIN32)
        const int proto = ::_dup(1);
        ::_dup2(2, 1);
#else
        const int proto = ::dup(1);
        ::dup2(2, 1);
#endif
        if (proto < 0) {
            return serve(std::cin, std::cout);
        }
        detail::FdOut buf(proto);
        std::ostream  out(&buf);
        return serve(std::cin, out);
    }

} // namespace rosetta::mcp
