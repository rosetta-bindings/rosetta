// Copyright (c) fmaerten@gmail.com
// License: MIT

// Bodies for <rosetta/runtime/mcp_python.h>. See that header for the design.

#pragma once

// Qt defines `slots` as a macro, and Python's object.h has a struct member of
// that name: hide the macro while Python's headers are read, so this can be
// included after Qt (as the Qt viewer's Claude panel does).
#pragma push_macro("slots")
#undef slots
#include <pybind11/embed.h>
#pragma pop_macro("slots")

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace rosetta::mcp {

    namespace py_detail {

        namespace py = pybind11;

        // The server the current run_python() call works against. One run at a
        // time (the GIL and the single interpreter thread guarantee it).
        inline Server *&current() {
            static Server *s = nullptr;
            return s;
        }
        inline Server &server() {
            if (!current()) {
                throw dyn::Error("rosetta objects are only usable inside run_python");
            }
            return *current();
        }

        struct Obj {
            dyn::Object obj;
        };
        struct Cls {
            const dyn::MetaClass *cls;
        };
        struct BoundMethod {
            dyn::Object obj;
            std::string name;
        };
        struct StaticMethod {
            const dyn::MetaClass *cls;
            std::string           name;
        };
        struct Classes {};

        // ---- Python <-> dyn::Any -------------------------------------------

        inline py::object to_py(const dyn::Any &v) {
            using dyn::Kind;
            if (v.empty()) {
                return py::none();
            }
            switch (v.kind()) {
            case Kind::void_:
                return py::none();
            case Kind::boolean:
                return py::bool_(v.as_bool());
            case Kind::number:
                if (v.type() && v.type()->integral) {
                    return py::int_(v.as_int());
                }
                return py::float_(v.as_number());
            case Kind::string:
                return py::str(v.as_string());
            case Kind::enum_: {
                const dyn::TypeDesc *t = v.type();
                for (std::size_t i = 0; t && i < t->n_enumerators; ++i) {
                    if (t->enumerators[i].value == v.as_int()) {
                        return py::str(t->enumerators[i].name);
                    }
                }
                return py::int_(v.as_int());
            }
            case Kind::vector: {
                const auto &l = v.as_list();
                py::list    out(l.size());
                for (std::size_t i = 0; i < l.size(); ++i) {
                    out[i] = to_py(l[i]);
                }
                return std::move(out);
            }
            case Kind::object: {
                const dyn::ObjectRef &r = v.as_object();
                if (!r.ptr || !r.cls) {
                    return py::none();
                }
                return py::cast(Obj{dyn::Object::adopt(*r.cls, r.ptr, r.owner)});
            }
            case Kind::unknown:
                break;
            }
            return py::str(std::string("<opaque ") + (v.type() ? v.type()->spelling : "?") + ">");
        }

        inline dyn::Any from_py(py::handle h) {
            using dyn::Any;
            if (h.is_none()) {
                return Any::none();
            }
            if (py::isinstance<py::bool_>(h)) { // before int: bool is an int in Python
                return Any::boolean(h.cast<bool>());
            }
            if (py::isinstance<py::int_>(h)) {
                return Any::integer(h.cast<long long>());
            }
            if (py::isinstance<py::float_>(h)) {
                return Any::real(h.cast<double>());
            }
            if (py::isinstance<py::str>(h)) {
                return Any::text(h.cast<std::string>());
            }
            if (py::isinstance<Obj>(h)) {
                return h.cast<Obj &>().obj.as_any();
            }
            if (py::hasattr(h, "tolist")) { // numpy arrays and scalars
                return from_py(h.attr("tolist")());
            }
            if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h)) {
                std::vector<Any> out;
                for (py::handle e : h) {
                    out.push_back(from_py(e));
                }
                return Any::list(std::move(out));
            }
            throw dyn::Error("cannot pass a Python " +
                             py::str(py::type::handle_of(h).attr("__name__")).cast<std::string>() +
                             " to C++");
        }

        /**
         * @brief Positional + keyword arguments for one of `sigs`.
         *
         * Keywords fill the parameters after the positional ones, by name. With
         * no keywords this is the server's own positional coercion.
         */
        inline dyn::ArgList args_of(const std::vector<Server::Signature> &sigs, py::args args,
                                    py::kwargs kwargs) {
            std::vector<dyn::Any> raw;
            for (py::handle a : args) {
                raw.push_back(from_py(a));
            }
            if (!kwargs || kwargs.empty()) {
                return server().coerce_args(std::move(raw), sigs);
            }

            const Server::Signature *hit = nullptr;
            std::vector<dyn::Any>     ordered;
            for (const Server::Signature &s : sigs) {
                if (dyn::detail::input_arity(s.params, s.n_params) != raw.size() + kwargs.size()) {
                    continue;
                }
                std::vector<dyn::Any> trial = raw;
                std::size_t           pos = 0, used = 0;
                bool                  ok = true;
                for (std::size_t i = 0; i < s.n_params && ok; ++i) {
                    if (s.params[i].is_out) {
                        continue;
                    }
                    if (pos++ < raw.size()) {
                        continue;
                    }
                    const char *n = s.params[i].name;
                    if (kwargs.contains(n)) {
                        trial.push_back(from_py(kwargs[n]));
                        ++used;
                    } else {
                        ok = false;
                    }
                }
                if (ok && used == kwargs.size()) {
                    if (hit) {
                        throw dyn::Error("keyword arguments match more than one overload");
                    }
                    hit     = &s;
                    ordered = std::move(trial);
                }
            }
            if (!hit) {
                throw dyn::Error("no overload takes these keyword arguments");
            }
            return server().coerce_args(std::move(ordered), {*hit});
        }

        inline py::object construct(const dyn::MetaClass &k, py::args args, py::kwargs kwargs) {
            dyn::Result r = dyn::Object::create(k, args_of(Server::ctor_signatures(k), args, kwargs));
            if (!r.ok()) {
                throw dyn::Error(r.error);
            }
            const dyn::ObjectRef &ref = r.value.as_object();
            return py::cast(Obj{dyn::Object::adopt(k, ref.ptr, ref.owner)});
        }

        inline const dyn::MetaClass &class_named(const std::string &n) {
            const dyn::MetaClass *k = server().registry().find_class(n);
            if (!k) {
                throw dyn::Error("no class '" + n + "'");
            }
            return *k;
        }

        /** @brief A script's result as tool-result JSON. */
        inline json to_json(py::handle h, int depth = 0) {
            if (depth > 32) {
                throw dyn::Error("result nests deeper than 32 levels (a cycle?)");
            }
            if (h.is_none()) {
                return nullptr;
            }
            if (py::isinstance<py::bool_>(h)) {
                return h.cast<bool>();
            }
            if (py::isinstance<py::int_>(h)) {
                try {
                    return h.cast<long long>();
                } catch (const py::cast_error &) {
                    return py::str(h).cast<std::string>(); // a bignum
                }
            }
            if (py::isinstance<py::float_>(h)) {
                return h.cast<double>();
            }
            if (py::isinstance<py::str>(h)) {
                return h.cast<std::string>();
            }
            if (py::isinstance<Obj>(h)) {
                return server().encode(h.cast<Obj &>().obj.as_any());
            }
            if (py::isinstance<Cls>(h)) {
                return h.cast<Cls &>().cls->qualified;
            }
            if (py::isinstance<py::dict>(h)) {
                json out = json::object();
                for (auto kv : h.cast<py::dict>()) {
                    out[py::str(kv.first).cast<std::string>()] = to_json(kv.second, depth + 1);
                }
                return out;
            }
            if (py::hasattr(h, "tolist") && !py::isinstance<py::list>(h)) {
                return to_json(h.attr("tolist")(), depth + 1);
            }
            if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h) ||
                py::isinstance<py::set>(h)) {
                std::vector<json> items;
                for (py::handle e : h) {
                    items.push_back(to_json(e, depth + 1));
                }
                return server().encode_list(std::move(items));
            }
            return py::repr(h).cast<std::string>();
        }

        // ---- the module ----------------------------------------------------

        // Runs the script as a notebook cell: the last expression is the value.
        constexpr const char *k_runner = R"PY(
def _run(src, g):
    import ast
    tree = ast.parse(src, "<script>", "exec")
    last = None
    if tree.body and isinstance(tree.body[-1], ast.Expr):
        last = ast.Expression(tree.body.pop().value)
    exec(compile(tree, "<script>", "exec"), g)
    if last is not None:
        return eval(compile(last, "<script>", "eval"), g)
    return g.get("result")
)PY";

        inline py::module_ &module() {
            static py::module_ *m = [] {
                static PyModuleDef def{};
                auto *mod = new py::module_(py::module_::create_extension_module(
                    "_rosetta_mcp", "rosetta objects for run_python", &def));
                py::module_ &m = *mod;

                py::class_<Obj>(m, "Object")
                    .def("__getattr__",
                         [](Obj &o, const std::string &name) -> py::object {
                             const dyn::MetaClass &k = *o.obj.meta();
                             if (const dyn::MetaField *f = k.field(name)) {
                                 dyn::Result r = o.obj.get(f->name);
                                 if (!r.ok()) {
                                     throw dyn::Error(r.error);
                                 }
                                 return to_py(r.value);
                             }
                             if (!k.overloads(name).empty()) {
                                 return py::cast(BoundMethod{o.obj, name});
                             }
                             throw py::attribute_error(std::string(k.name) +
                                                       " has no field or method '" + name + "'");
                         })
                    .def("__setattr__",
                         [](Obj &o, const std::string &name, py::object v) {
                             const dyn::MetaField *f = o.obj.meta()->field(name);
                             if (!f) {
                                 throw py::attribute_error(std::string(o.obj.meta()->name) +
                                                           " has no field '" + name + "'");
                             }
                             dyn::Result r = o.obj.set(name, server().coerce(from_py(v), f->type));
                             if (!r.ok()) {
                                 throw dyn::Error(r.error);
                             }
                         })
                    .def("__dir__",
                         [](Obj &o) {
                             py::list out;
                             const dyn::MetaClass *k = o.obj.meta();
                             for (std::size_t i = 0; i < k->n_fields; ++i) {
                                 out.append(k->fields[i].name);
                             }
                             for (std::size_t i = 0; i < k->n_methods; ++i) {
                                 if (k->methods[i].overload_index == 0) { // once per overload set
                                     out.append(k->methods[i].name);
                                 }
                             }
                             return out;
                         })
                    .def("__repr__",
                         [](Obj &o) {
                             char buf[32];
                             std::snprintf(buf, sizeof buf, "%p", o.obj.ptr());
                             return std::string("<") + o.obj.meta()->qualified + " " + buf + ">";
                         })
                    .def("__eq__", [](Obj &a, py::object b) {
                        return py::isinstance<Obj>(b) && b.cast<Obj &>().obj.ptr() == a.obj.ptr();
                    })
                    .def("__hash__", [](Obj &o) { return reinterpret_cast<std::uintptr_t>(o.obj.ptr()); });

                py::class_<BoundMethod>(m, "Method")
                    .def("__call__", [](BoundMethod &b, py::args args, py::kwargs kwargs) {
                        dyn::ArgList a =
                            args_of(Server::method_signatures(*b.obj.meta(), b.name), args, kwargs);
                        dyn::Result r = b.obj.call(b.name, a);
                        if (!r.ok()) {
                            throw dyn::Error(r.error);
                        }
                        return to_py(r.value);
                    });

                py::class_<StaticMethod>(m, "StaticMethod")
                    .def("__call__", [](StaticMethod &s, py::args args, py::kwargs kwargs) {
                        dyn::ArgList a =
                            args_of(Server::method_signatures(*s.cls, s.name), args, kwargs);
                        dyn::Result r = dyn::call_static(*s.cls, s.name, a);
                        if (!r.ok()) {
                            throw dyn::Error(r.error);
                        }
                        return to_py(r.value);
                    });

                py::class_<Cls>(m, "Class")
                    .def("__getattr__",
                         [](Cls &c, const std::string &name) {
                             if (Server::method_signatures(*c.cls, name).empty()) {
                                 throw py::attribute_error(std::string(c.cls->name) +
                                                           " has no static method '" + name + "'");
                             }
                             return StaticMethod{c.cls, name};
                         })
                    .def("__call__",
                         [](Cls &c, py::args args, py::kwargs kwargs) {
                             return construct(*c.cls, args, kwargs);
                         })
                    .def("__repr__",
                         [](Cls &c) { return std::string("<class ") + c.cls->qualified + ">"; });

                py::class_<Classes>(m, "Classes")
                    .def("__getattr__", [](Classes &, const std::string &n) { return Cls{&class_named(n)}; })
                    .def("__getitem__", [](Classes &, const std::string &n) { return Cls{&class_named(n)}; })
                    .def("__dir__", [](Classes &) {
                        py::list out;
                        for (const dyn::MetaClass *k : server().registry().classes()) {
                            out.append(k->name);
                        }
                        return out;
                    });

                m.def("obj", [](const std::string &h) {
                    const dyn::Object *o = server().find(h);
                    if (!o) {
                        throw dyn::Error("obj(): no object named '" + h + "'");
                    }
                    return Obj{*o};
                });
                m.def("new", [](const std::string &cls, py::args args, py::kwargs kwargs) {
                    return construct(class_named(cls), args, kwargs);
                });
                m.def("keep", [](py::object name, Obj &o) {
                    return server().store(o.obj, name.is_none() ? std::string()
                                                                : name.cast<std::string>());
                });
                m.def("call", [](const std::string &fn, py::args args) {
                    const auto fns = server().registry().function_overloads(fn);
                    if (fns.empty()) {
                        throw dyn::Error("call(): no function '" + fn + "'");
                    }
                    std::vector<Server::Signature> sigs;
                    for (const dyn::MetaFunction *f : fns) {
                        sigs.push_back({f->params, f->n_params});
                    }
                    dyn::ArgList a = args_of(sigs, args, py::kwargs());
                    std::string  why;
                    for (const dyn::MetaFunction *f : fns) {
                        dyn::Result r = dyn::call_function(*f, a);
                        if (r.ok()) {
                            return to_py(r.value);
                        }
                        why += "\n" + r.error;
                    }
                    throw dyn::Error(why.substr(1));
                });
                m.attr("classes") = Classes{};

                py::exec(k_runner, m.attr("__dict__"));
                py::module_::import("sys").attr("modules")["_rosetta_mcp"] = m;
                return mod;
            }();
            return *m;
        }

        // ---- timeout -------------------------------------------------------

        // The run a pending TimeoutError belongs to. A pending call queued for
        // a run that has already finished must not fire into the next one.
        inline std::atomic<std::intptr_t> &active_run() {
            static std::atomic<std::intptr_t> id{0};
            return id;
        }

        inline int raise_timeout(void *run) {
            if (reinterpret_cast<std::intptr_t>(run) != active_run().load()) {
                return 0;
            }
            PyErr_SetString(PyExc_TimeoutError, "script exceeded its time limit");
            return -1;
        }

        /** @brief Arms a TimeoutError for `timeout_s`; disarms on destruction. */
        class Watchdog {
        public:
            Watchdog(std::intptr_t id, double timeout_s) : id_(id) {
                active_run() = id;
                thread_ = std::thread([this, timeout_s] {
                    std::unique_lock<std::mutex> lk(mu_);
                    if (!cv_.wait_for(lk, std::chrono::duration<double>(timeout_s),
                                      [this] { return done_; })) {
                        lk.unlock();
                        Py_AddPendingCall(raise_timeout, reinterpret_cast<void *>(id_));
                        // A pending call scheduled from a thread that never holds
                        // the GIL does not wake the running loop (CPython 3.12:
                        // the eval breaker is recomputed for the CALLING thread).
                        // Taking the GIL once forces a switch, and the main
                        // thread re-checks its pending calls when it resumes.
                        const PyGILState_STATE st = PyGILState_Ensure();
                        PyGILState_Release(st);
                    }
                });
            }
            ~Watchdog() {
                active_run() = 0;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    done_ = true;
                }
                cv_.notify_one();
                // The worker may be waiting for the GIL (it fired just as the
                // script finished): let go of it while joining.
                pybind11::gil_scoped_release release;
                thread_.join();
            }

        private:
            std::intptr_t           id_;
            std::mutex              mu_;
            std::condition_variable cv_;
            bool                    done_ = false;
            std::thread             thread_;
        };

        /** @brief Swaps sys.stdout/stderr for a StringIO, and back. */
        struct Capture {
            py::object sys, out, err, buf;
            Capture() {
                sys = py::module_::import("sys");
                out = sys.attr("stdout");
                err = sys.attr("stderr");
                buf = py::module_::import("io").attr("StringIO")();
                sys.attr("stdout") = buf;
                sys.attr("stderr") = buf;
            }
            ~Capture() {
                sys.attr("stdout") = out;
                sys.attr("stderr") = err;
            }
            std::string text(std::size_t max, bool &truncated) const {
                std::string s = buf.attr("getvalue")().cast<std::string>();
                truncated     = s.size() > max;
                if (truncated) {
                    s.resize(max);
                }
                return s;
            }
        };

    } // namespace py_detail

    // ---------------------------------------------------------------------

    inline json run_python(Server &server, const std::string &code, double timeout_s,
                           const PythonOptions &opts) {
        namespace py = pybind11;
        using namespace py_detail;

        if (!Py_IsInitialized()) {
            py::initialize_interpreter(); // never finalized — see the header
        }
        py::gil_scoped_acquire gil;

        struct Scope { // current() for the duration of the call, even on throw
            explicit Scope(Server &s) { current() = &s; }
            ~Scope() { current() = nullptr; }
        } scope(server);

        static std::intptr_t runs = 0;
        const auto           t0   = std::chrono::steady_clock::now();
        py::module_         &mod  = module();

        py::dict g;
        g["__builtins__"] = py::module_::import("builtins");
        g["__name__"]     = "__main__";
        for (const char *name : {"obj", "new", "keep", "call", "classes"}) {
            g[name] = mod.attr(name);
        }

        std::string output;
        bool        truncated = false;
        json        result;
        {
            Capture cap;
            try {
                py::object value;
                {
                    Watchdog dog(++runs, timeout_s);
                    value = mod.attr("_run")(code, g);
                }
                result = to_json(value);
                output = cap.text(opts.max_output, truncated);
            } catch (py::error_already_set &e) {
                output          = cap.text(opts.max_output, truncated);
                std::string msg = std::string("script error: ") + e.what();
                if (!output.empty()) {
                    msg += "\n--- output before the error ---\n" + output;
                }
                throw dyn::Error(msg);
            }
        }

        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - t0;
        json out = {{"result", result}, {"elapsed_s", elapsed.count()}};
        if (!output.empty()) {
            out["output"] = output;
        }
        if (truncated) {
            out["output_truncated"] = true;
        }
        return out;
    }

    inline void enable_python(Server &server, PythonOptions opts) {
        json def = {
            {"name", "run_python"},
            {"description",
             "Run a Python 3 script inside the server process, next to the live C++ objects — "
             "for loops, searches and analysis that would otherwise take many tool calls; "
             "numpy and the rest of the installed environment are importable. API (predefined "
             "globals): obj(\"handle\") fetches a live object; new(\"Class\", *args, **kw) or "
             "classes.Class(*args) constructs one; o.field reads, o.field = v writes (ranges and "
             "read-only enforced); o.method(*args, **kw) calls a method; "
             "classes.Class.staticMethod(*args) a static one; call(\"function\", *args) a free "
             "function; keep(\"name\", o) publishes an object as a handle for later tool calls. "
             "Enums are strings (enumerator names); vectors are Python lists, passed whole. The "
             "value of the LAST EXPRESSION is the result (else a global named `result`); objects "
             "become handles, long arrays are summarized; print() is captured. A rejected call "
             "raises RuntimeError. NOT sandboxed: the script runs with the server's own "
             "permissions."},
            {"inputSchema",
             {{"type", "object"},
              {"properties",
               {{"code", {{"type", "string"}, {"description", "Python source"}}},
                {"timeout_s",
                 {{"type", "number"},
                  {"description", "Wall-clock limit in seconds (default " +
                                       std::to_string(static_cast<int>(opts.default_timeout_s)) +
                                       ", max " +
                                       std::to_string(static_cast<int>(opts.max_timeout_s)) +
                                       ")"}}}}},
              {"required", {"code"}}}}};

        server.add_tool(std::move(def), [&server, opts](const json &a) {
            if (!a.contains("code") || !a["code"].is_string()) {
                throw dyn::Error("missing string argument 'code'");
            }
            double t = opts.default_timeout_s;
            if (a.contains("timeout_s") && a["timeout_s"].is_number()) {
                t = std::clamp(a["timeout_s"].get<double>(), 0.001, opts.max_timeout_s);
            }
            return run_python(server, a["code"].get<std::string>(), t, opts);
        });
    }

} // namespace rosetta::mcp
