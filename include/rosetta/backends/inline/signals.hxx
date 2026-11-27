// Copyright (c) fmaerten@gmail.com
// License: MIT

// Signal members (rosetta/signal.h) for the backends that bind them.
//
// The walk records every public member of a signal type as a GenSignal, whose
// `connect` is an ordinary extension method — receiver, one callback
// parameter — calling a THUNK that does not exist anywhere yet. A backend that
// binds signals emits the thunks it uses (signal_thunks_cpp) into every
// translation unit that names them, then binds `connect` through its existing
// extension-with-callback path. Nothing else in the backend changes.
//
// Included by the python backend (inline/python.hxx) right after qualify_std,
// which the spellings go through; the later backends see it from there.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail;

        // Is `t` (an object type) one of the bound classes?
        inline bool sig_names_bound_class(const GenType &t, const GenContext &c) {
            if (t.kind != "object") {
                return false;
            }
            for (const auto &k : c.classes) {
                if (t.object_qualified.empty() ? k.name == t.object
                                               : qualified_of(k) == t.object_qualified) {
                    return true;
                }
            }
            return false;
        }

        // The connect method as a backend binds it: returning the connection only
        // when its type is a bound class — a script cannot hold anything else, and
        // returning it anyway would make the whole method unbindable.
        inline GenMethod signal_method(const GenSignal &s, const GenContext &c) {
            GenMethod m = s.connect;
            if (m.ret.kind != "void" && !sig_names_bound_class(m.ret, c)) {
                m.ret     = GenType{};
                m.ret.kind = "void";
                m.ret_cpp = "void";
            }
            return m;
        }

        // Does the signal's method name collide with a member the class already
        // binds (or an earlier signal)? The explicit member wins.
        inline bool signal_name_taken(const GenClass &k, const GenSignal &s) {
            const std::string &n = s.connect.name;
            for (const auto &m : k.methods) {
                if (m.name == n) {
                    return true;
                }
            }
            for (const auto &f : k.fields) {
                if (f.name == n) {
                    return true;
                }
            }
            for (const auto &o : k.signals) {
                if (&o == &s) {
                    break;
                }
                if (o.connect.name == n) {
                    return true;
                }
            }
            return false;
        }

        // Compilable spellings of the callback parameter and the return.
        inline std::string signal_fn_cpp(const GenMethod &m) {
            return qualify_objects(qualify_std(m.param_cpp.front()), m.params.front().type);
        }
        inline std::string signal_ret_cpp(const GenMethod &m) {
            return m.ret.kind == "void" ? std::string("void")
                                        : qualify_objects(qualify_std(m.ret_cpp), m.ret);
        }

        // signal_traits<S>::connect with $signal / $self / $fn expanded for a
        // thunk whose parameters are `self` and `fn`.
        inline std::string signal_connect_expr(const GenSignal &s) {
            std::string e = s.connect_expr;
            const std::pair<std::string, std::string> subst[] = {
                {"$signal", "self." + s.field}, {"$self", "self"}, {"$fn", "std::move(fn)"}};
            for (const auto &[key, value] : subst) {
                for (std::string::size_type p = 0; (p = e.find(key, p)) != std::string::npos;
                     p += value.size()) {
                    e.replace(p, key.size(), value);
                }
            }
            return e;
        }

        // The thunks of the signals `bound(k, s)` accepts, as one namespace.
        // Inline functions: every part of a sharded binding may define them.
        template <typename Pred>
        inline std::string signal_thunks_cpp(const GenContext &c, Pred bound) {
            std::string body;
            for (const auto &k : c.classes) {
                for (const auto &s : k.signals) {
                    if (!bound(k, s)) {
                        continue;
                    }
                    const GenMethod   m    = signal_method(s, c);
                    const std::string name = m.ext_qualified.substr(m.ext_qualified.rfind("::") + 2);
                    const std::string ret  = signal_ret_cpp(m);
                    body += "    inline " + ret + " " + name + "(" + qualified_of(k) +
                            " &self, " + signal_fn_cpp(m) + " fn) {\n        " +
                            (ret == "void" ? "" : "return ") + signal_connect_expr(s) +
                            ";\n    }\n";
                }
            }
            if (body.empty()) {
                return {};
            }
            return "\n// Signal connect thunks (rosetta/signal.h): one per bound signal member.\n"
                   "#include <functional>\n"
                   "#include <utility>\n"
                   "namespace rosetta_sig {\n" +
                   body + "}\n";
        }

    } // namespace backend
} // namespace rosetta
