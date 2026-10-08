// Copyright (c) fmaerten@gmail.com
// License: MIT

// Bodies for <rosetta/runtime/mcp_lua.h>. See that header for the design.
//
// ONE RULE governs everything below: Lua reports errors with longjmp, which
// skips C++ destructors. So no Lua function that can raise an error is called
// while a C++ object with a destructor is alive in the same frame. Every
// lua_CFunction is wrapped in safe<>, which runs the body inside try/catch,
// turns a C++ exception into a pushed message, and only calls lua_error()
// once the body's frame — and every C++ local in it — is gone. Inside the
// bodies, only non-raising API calls are used (rawget, testudata, tolstring
// on known strings — never luaL_check*, lua_gettable or luaL_tolstring).

#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>

#if LUA_VERSION_NUM < 503
#error "rosetta's run_script needs Lua 5.3 or newer (integers, lua_getextraspace)"
#endif

namespace rosetta::mcp {

    namespace lua_detail {

        using Clock = std::chrono::steady_clock;

        constexpr const char *k_object_mt = "rosetta.object";
        constexpr const char *k_class_mt  = "rosetta.class";

        /** @brief Per-call state, reachable from any lua_State via its extra space. */
        struct Run {
            Server           *server = nullptr;
            const LuaOptions *opts   = nullptr;
            std::string       output;
            bool              output_truncated = false;
            Clock::time_point start;
            Clock::time_point deadline;
            double            timeout_s = 0;
            std::size_t       memory    = 0;
            lua_Alloc         base_alloc = nullptr;
            void             *base_ud    = nullptr;
        };

        inline Run &run_of(lua_State *L) { return **static_cast<Run **>(lua_getextraspace(L)); }

        struct ObjectBox {
            dyn::Object obj;
        };
        struct ClassBox {
            const dyn::MetaClass *cls;
        };

        // ---- the error discipline ------------------------------------------

        using Body = int (*)(lua_State *, Run &);

        template <Body F> int safe(lua_State *L) {
            bool failed = false;
            int  n      = 0;
            try {
                n = F(L, run_of(L));
            } catch (const std::exception &e) {
                lua_pushstring(L, e.what());
                failed = true;
            } catch (...) {
                lua_pushstring(L, "unknown C++ exception");
                failed = true;
            }
            if (failed) {
                return lua_error(L); // no C++ locals left in this frame
            }
            return n;
        }

        inline std::string str_at(lua_State *L, int i, const char *what) {
            if (lua_type(L, i) != LUA_TSTRING) {
                throw dyn::Error(std::string(what) + " must be a string");
            }
            std::size_t n = 0;
            const char *s = lua_tolstring(L, i, &n);
            return std::string(s, n);
        }

        inline ObjectBox *object_at(lua_State *L, int i) {
            return static_cast<ObjectBox *>(luaL_testudata(L, i, k_object_mt));
        }
        inline ClassBox *class_at(lua_State *L, int i) {
            return static_cast<ClassBox *>(luaL_testudata(L, i, k_class_mt));
        }

        inline const dyn::MetaClass &class_named(Run &run, const std::string &name) {
            const dyn::MetaClass *k = run.server->registry().find_class(name);
            if (!k) {
                throw dyn::Error("no class '" + name + "'");
            }
            return *k;
        }

        // ---- Lua <-> dyn::Any --------------------------------------------

        inline void push_object(lua_State *L, dyn::Object o) {
            void *mem = lua_newuserdata(L, sizeof(ObjectBox));
            new (mem) ObjectBox{std::move(o)};
            luaL_setmetatable(L, k_object_mt);
        }

        inline void push_any(lua_State *L, const dyn::Any &v) {
            using dyn::Kind;
            if (v.empty()) {
                lua_pushnil(L);
                return;
            }
            switch (v.kind()) {
            case Kind::void_:
                lua_pushnil(L);
                return;
            case Kind::boolean:
                lua_pushboolean(L, v.as_bool());
                return;
            case Kind::number:
                if (v.type() && v.type()->integral) {
                    lua_pushinteger(L, static_cast<lua_Integer>(v.as_int()));
                } else {
                    lua_pushnumber(L, v.as_number());
                }
                return;
            case Kind::string:
                lua_pushlstring(L, v.as_string().data(), v.as_string().size());
                return;
            case Kind::enum_: {
                // By name, as everywhere else in the MCP surface.
                const dyn::TypeDesc *t = v.type();
                for (std::size_t i = 0; t && i < t->n_enumerators; ++i) {
                    if (t->enumerators[i].value == v.as_int()) {
                        lua_pushstring(L, t->enumerators[i].name);
                        return;
                    }
                }
                lua_pushinteger(L, static_cast<lua_Integer>(v.as_int()));
                return;
            }
            case Kind::vector: {
                const auto &l = v.as_list();
                lua_createtable(L, static_cast<int>(l.size()), 0);
                for (std::size_t i = 0; i < l.size(); ++i) {
                    push_any(L, l[i]);
                    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
                }
                return;
            }
            case Kind::object: {
                const dyn::ObjectRef &r = v.as_object();
                if (!r.ptr || !r.cls) {
                    lua_pushnil(L);
                    return;
                }
                push_object(L, dyn::Object::adopt(*r.cls, r.ptr, r.owner));
                return;
            }
            case Kind::unknown:
                break;
            }
            lua_pushfstring(L, "<opaque %s>", v.type() ? v.type()->spelling : "?");
        }

        inline dyn::Any to_any(lua_State *L, int i) {
            using dyn::Any;
            i = lua_absindex(L, i);
            switch (lua_type(L, i)) {
            case LUA_TNIL:
                return Any::none();
            case LUA_TBOOLEAN:
                return Any::boolean(lua_toboolean(L, i) != 0);
            case LUA_TNUMBER:
                if (lua_isinteger(L, i)) {
                    return Any::integer(static_cast<long long>(lua_tointeger(L, i)));
                }
                return Any::real(static_cast<double>(lua_tonumber(L, i)));
            case LUA_TSTRING: {
                std::size_t n = 0;
                const char *s = lua_tolstring(L, i, &n);
                return Any::text(std::string(s, n));
            }
            case LUA_TUSERDATA:
                if (ObjectBox *b = object_at(L, i)) {
                    return b->obj.as_any();
                }
                break;
            case LUA_TTABLE: {
                const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, i));
                std::vector<Any>  out;
                out.reserve(static_cast<std::size_t>(n));
                for (lua_Integer k = 1; k <= n; ++k) {
                    lua_rawgeti(L, i, k);
                    out.push_back(to_any(L, -1));
                    lua_pop(L, 1);
                }
                return Any::list(std::move(out));
            }
            default:
                break;
            }
            throw dyn::Error(std::string("cannot pass a Lua ") + lua_typename(L, lua_type(L, i)) +
                             " to C++");
        }

        inline std::vector<dyn::Any> args_from(lua_State *L, int first) {
            std::vector<dyn::Any> raw;
            for (int i = first; i <= lua_gettop(L); ++i) {
                raw.push_back(to_any(L, i));
            }
            return raw;
        }

        /** @brief A script's return value as tool-result JSON. */
        inline json to_json(lua_State *L, int i, Run &run, int depth = 0) {
            i = lua_absindex(L, i);
            if (depth > 32) {
                throw dyn::Error("returned value nests deeper than 32 levels (a cycle?)");
            }
            switch (lua_type(L, i)) {
            case LUA_TNIL:
                return nullptr;
            case LUA_TBOOLEAN:
                return lua_toboolean(L, i) != 0;
            case LUA_TNUMBER:
                if (lua_isinteger(L, i)) {
                    return static_cast<long long>(lua_tointeger(L, i));
                }
                return static_cast<double>(lua_tonumber(L, i));
            case LUA_TSTRING: {
                std::size_t n = 0;
                const char *s = lua_tolstring(L, i, &n);
                return std::string(s, n);
            }
            case LUA_TUSERDATA:
                if (ObjectBox *b = object_at(L, i)) {
                    return run.server->encode(b->obj.as_any()); // becomes a handle
                }
                if (ClassBox *c = class_at(L, i)) {
                    return c->cls->qualified;
                }
                break;
            case LUA_TTABLE: {
                const std::size_t n     = lua_rawlen(L, i);
                std::size_t       count = 0;
                bool              array = true;
                lua_pushnil(L);
                while (lua_next(L, i) != 0) {
                    ++count;
                    if (!lua_isinteger(L, -2)) {
                        array = false;
                    }
                    lua_pop(L, 1);
                }
                if (array && count == n) {
                    std::vector<json> items;
                    items.reserve(n);
                    for (std::size_t k = 1; k <= n; ++k) {
                        lua_rawgeti(L, i, static_cast<lua_Integer>(k));
                        items.push_back(to_json(L, -1, run, depth + 1));
                        lua_pop(L, 1);
                    }
                    return run.server->encode_list(std::move(items));
                }
                json out = json::object();
                lua_pushnil(L);
                while (lua_next(L, i) != 0) {
                    std::string key;
                    if (lua_type(L, -2) == LUA_TSTRING) {
                        std::size_t kn = 0;
                        const char *ks = lua_tolstring(L, -2, &kn);
                        key.assign(ks, kn);
                    } else if (lua_isinteger(L, -2)) {
                        key = std::to_string(lua_tointeger(L, -2));
                    } else if (lua_type(L, -2) == LUA_TNUMBER) {
                        key = std::to_string(lua_tonumber(L, -2));
                    } else {
                        key = std::string("<") + lua_typename(L, lua_type(L, -2)) + ">";
                    }
                    out[key] = to_json(L, -1, run, depth + 1);
                    lua_pop(L, 1);
                }
                return out;
            }
            default:
                break;
            }
            return std::string("<") + lua_typename(L, lua_type(L, i)) + ">";
        }

        // ---- calls ---------------------------------------------------------

        inline int construct(lua_State *L, Run &run, const dyn::MetaClass &k, int first) {
            dyn::ArgList args =
                run.server->coerce_args(args_from(L, first), Server::ctor_signatures(k));
            dyn::Result r = dyn::Object::create(k, args);
            if (!r.ok()) {
                throw dyn::Error(r.error);
            }
            const dyn::ObjectRef &ref = r.value.as_object();
            push_object(L, dyn::Object::adopt(k, ref.ptr, ref.owner));
            return 1;
        }

        // o:method(...)  — upvalue 1 is the method name.
        inline int method_call(lua_State *L, Run &run) {
            const std::string name = str_at(L, lua_upvalueindex(1), "method name");
            ObjectBox        *self = object_at(L, 1);
            if (!self || self->obj.meta()->overloads(name).empty()) {
                throw dyn::Error("call methods with ':' — obj:" + name + "(...), not obj." + name +
                                 "(...)");
            }
            dyn::ArgList args = run.server->coerce_args(
                args_from(L, 2), Server::method_signatures(*self->obj.meta(), name));
            dyn::Result r = self->obj.call(name, args);
            if (!r.ok()) {
                throw dyn::Error(r.error);
            }
            push_any(L, r.value);
            return 1;
        }

        // classes.Relaxer.minQuality(...)  — upvalue 1 is the class box, 2 the name.
        inline int static_call(lua_State *L, Run &run) {
            ClassBox         *c    = class_at(L, lua_upvalueindex(1));
            const std::string name = str_at(L, lua_upvalueindex(2), "method name");
            // Tolerate classes.X:method(...) by dropping the class itself.
            const int    first = class_at(L, 1) == c ? 2 : 1;
            dyn::ArgList args  = run.server->coerce_args(
                args_from(L, first), Server::method_signatures(*c->cls, name));
            dyn::Result r = dyn::call_static(*c->cls, name, args);
            if (!r.ok()) {
                throw dyn::Error(r.error);
            }
            push_any(L, r.value);
            return 1;
        }

        // ---- metamethods ---------------------------------------------------

        inline int object_index(lua_State *L, Run &) {
            ObjectBox        *self = object_at(L, 1);
            const std::string key  = str_at(L, 2, "member name");
            const dyn::MetaClass &k = *self->obj.meta();
            if (const dyn::MetaField *f = k.field(key)) {
                dyn::Result r = self->obj.get(f->name);
                if (!r.ok()) {
                    throw dyn::Error(r.error);
                }
                push_any(L, r.value);
                return 1;
            }
            if (!k.overloads(key).empty()) {
                lua_pushvalue(L, 2);
                lua_pushcclosure(L, safe<method_call>, 1);
                return 1;
            }
            throw dyn::Error(std::string(k.name) + " has no field or method '" + key + "'");
        }

        inline int object_newindex(lua_State *L, Run &run) {
            ObjectBox            *self = object_at(L, 1);
            const std::string     key  = str_at(L, 2, "field name");
            const dyn::MetaField *f    = self->obj.meta()->field(key);
            if (!f) {
                throw dyn::Error(std::string(self->obj.meta()->name) + " has no field '" + key + "'");
            }
            dyn::Result r = self->obj.set(key, run.server->coerce(to_any(L, 3), f->type));
            if (!r.ok()) {
                throw dyn::Error(r.error);
            }
            return 0;
        }

        inline int object_tostring(lua_State *L, Run &) {
            ObjectBox *self = object_at(L, 1);
            lua_pushfstring(L, "<%s %p>", self->obj.meta()->qualified, self->obj.ptr());
            return 1;
        }

        inline int object_eq(lua_State *L, Run &) {
            ObjectBox *a = object_at(L, 1), *b = object_at(L, 2);
            lua_pushboolean(L, a && b && a->obj.ptr() == b->obj.ptr());
            return 1;
        }

        inline int object_gc(lua_State *L, Run &) {
            if (ObjectBox *self = object_at(L, 1)) {
                self->~ObjectBox();
            }
            return 0;
        }

        inline int class_index(lua_State *L, Run &) {
            ClassBox         *c   = class_at(L, 1);
            const std::string key = str_at(L, 2, "method name");
            if (Server::method_signatures(*c->cls, key).empty()) {
                throw dyn::Error(std::string(c->cls->name) + " has no static method '" + key + "'");
            }
            lua_pushvalue(L, 1);
            lua_pushvalue(L, 2);
            lua_pushcclosure(L, safe<static_call>, 2);
            return 1;
        }

        inline int class_call(lua_State *L, Run &run) {
            return construct(L, run, *class_at(L, 1)->cls, 2); // classes.Relaxer(...)
        }

        inline int class_tostring(lua_State *L, Run &) {
            lua_pushfstring(L, "<class %s>", class_at(L, 1)->cls->qualified);
            return 1;
        }

        // classes.Relaxer / classes["scene::Mesh"]
        inline int classes_index(lua_State *L, Run &run) {
            const dyn::MetaClass &k   = class_named(run, str_at(L, 2, "class name"));
            void                 *mem = lua_newuserdata(L, sizeof(ClassBox));
            new (mem) ClassBox{&k};
            luaL_setmetatable(L, k_class_mt);
            return 1;
        }

        // ---- globals -------------------------------------------------------

        inline int g_new(lua_State *L, Run &run) {
            return construct(L, run, class_named(run, str_at(L, 1, "new(): class name")), 2);
        }

        inline int g_obj(lua_State *L, Run &run) {
            const std::string  h = str_at(L, 1, "obj(): handle");
            const dyn::Object *o = run.server->find(h);
            if (!o) {
                throw dyn::Error("obj(): no object named '" + h + "'");
            }
            push_object(L, *o);
            return 1;
        }

        inline int g_keep(lua_State *L, Run &run) {
            ObjectBox *b = object_at(L, 2);
            if (!b) {
                throw dyn::Error("keep(name, object): the second argument must be an object");
            }
            const std::string name =
                lua_isnil(L, 1) ? std::string() : str_at(L, 1, "keep(): handle name");
            const std::string h = run.server->store(b->obj, name);
            lua_pushlstring(L, h.data(), h.size());
            return 1;
        }

        inline int g_call(lua_State *L, Run &run) {
            const std::string name = str_at(L, 1, "call(): function name");
            const auto        fns  = run.server->registry().function_overloads(name);
            if (fns.empty()) {
                throw dyn::Error("call(): no function '" + name + "'");
            }
            std::vector<Server::Signature> sigs;
            for (const dyn::MetaFunction *f : fns) {
                sigs.push_back({f->params, f->n_params});
            }
            dyn::ArgList args = run.server->coerce_args(args_from(L, 2), sigs);
            std::string  why;
            for (const dyn::MetaFunction *f : fns) {
                dyn::Result r = dyn::call_function(*f, args);
                if (r.ok()) {
                    push_any(L, r.value);
                    return 1;
                }
                why += "\n" + r.error;
            }
            throw dyn::Error(why.substr(1));
        }

        inline int g_clock(lua_State *L, Run &run) {
            const std::chrono::duration<double> d = Clock::now() - run.start;
            lua_pushnumber(L, d.count());
            return 1;
        }

        // print(): captured, not written to stdout (which carries the protocol).
        // Arguments are formatted with plain tostring semantics for the basic
        // types and __tostring for ours — both non-raising.
        inline int g_print(lua_State *L, Run &run) {
            std::string line;
            for (int i = 1; i <= lua_gettop(L); ++i) {
                if (i > 1) {
                    line += '\t';
                }
                switch (lua_type(L, i)) {
                case LUA_TSTRING:
                case LUA_TNUMBER: {
                    lua_pushvalue(L, i); // tolstring converts in place; work on a copy
                    std::size_t n = 0;
                    const char *s = lua_tolstring(L, -1, &n);
                    line.append(s, n);
                    lua_pop(L, 1);
                    break;
                }
                case LUA_TBOOLEAN:
                    line += lua_toboolean(L, i) ? "true" : "false";
                    break;
                case LUA_TNIL:
                    line += "nil";
                    break;
                default:
                    if (ObjectBox *b = object_at(L, i)) {
                        line += std::string("<") + b->obj.meta()->qualified + ">";
                    } else if (ClassBox *c = class_at(L, i)) {
                        line += std::string("<class ") + c->cls->qualified + ">";
                    } else {
                        line += std::string("<") + lua_typename(L, lua_type(L, i)) + ">";
                    }
                }
            }
            line += '\n';
            const std::size_t room =
                run.output.size() < run.opts->max_output ? run.opts->max_output - run.output.size() : 0;
            if (line.size() > room) {
                run.output.append(line, 0, room);
                run.output_truncated = true;
            } else {
                run.output += line;
            }
            return 0;
        }

        // ---- limits --------------------------------------------------------

        inline void timeout_hook(lua_State *L, lua_Debug *) {
            Run &run = run_of(L);
            if (Clock::now() > run.deadline) {
                luaL_error(L, "script exceeded its %f s time limit", run.timeout_s);
            }
        }

        inline void *capped_alloc(void *ud, void *ptr, std::size_t osize, std::size_t nsize) {
            Run              &run = *static_cast<Run *>(ud);
            const std::size_t old = ptr ? osize : 0;
            if (nsize > old && run.memory + (nsize - old) > run.opts->max_memory) {
                return nullptr; // Lua raises "not enough memory"
            }
            void *p = run.base_alloc(run.base_ud, ptr, osize, nsize);
            if (p || nsize == 0) {
                // Blocks allocated before the cap was installed were never
                // counted, so freeing one must not wrap the counter.
                run.memory = (old > run.memory ? 0 : run.memory - old) + nsize;
            }
            return p;
        }

        inline int traceback(lua_State *L) {
            const char *msg = lua_tostring(L, 1);
            luaL_traceback(L, L, msg ? msg : "(error object is not a string)", 1);
            return 1;
        }

        inline void set_global(lua_State *L, const char *name, lua_CFunction f) {
            lua_pushcfunction(L, f);
            lua_setglobal(L, name);
        }

        /** @brief Owns the lua_State so every exit path closes it. */
        struct State {
            lua_State *L = nullptr;
            ~State() {
                if (L) {
                    lua_close(L);
                }
            }
        };

    } // namespace lua_detail

    // ---------------------------------------------------------------------

    inline json run_lua(Server &server, const std::string &code, double timeout_s,
                        const LuaOptions &opts) {
        using namespace lua_detail;

        Run run;
        run.server    = &server;
        run.opts      = &opts;
        run.timeout_s = timeout_s;
        run.start     = Clock::now();
        run.deadline  = run.start + std::chrono::duration_cast<Clock::duration>(
                                       std::chrono::duration<double>(timeout_s));

        State st;
        st.L = luaL_newstate();
        if (!st.L) {
            throw dyn::Error("could not create a Lua state");
        }
        lua_State *L = st.L;
        run.base_alloc = lua_getallocf(L, &run.base_ud);
        lua_setallocf(L, capped_alloc, &run);
        *static_cast<Run **>(lua_getextraspace(L)) = &run;

        // The sandbox: computation only.
        luaL_requiref(L, "_G", luaopen_base, 1);
        luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
        luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
        luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
        luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
        luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
        lua_settop(L, 0);
        for (const char *banned : {"dofile", "loadfile", "load", "require"}) {
            lua_pushnil(L);
            lua_setglobal(L, banned);
        }

        luaL_newmetatable(L, k_object_mt);
        lua_pushcfunction(L, safe<object_index>);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, safe<object_newindex>);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, safe<object_tostring>);
        lua_setfield(L, -2, "__tostring");
        lua_pushcfunction(L, safe<object_eq>);
        lua_setfield(L, -2, "__eq");
        lua_pushcfunction(L, safe<object_gc>);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);

        luaL_newmetatable(L, k_class_mt);
        lua_pushcfunction(L, safe<class_index>);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, safe<class_call>);
        lua_setfield(L, -2, "__call");
        lua_pushcfunction(L, safe<class_tostring>);
        lua_setfield(L, -2, "__tostring");
        lua_pop(L, 1);

        lua_newtable(L); // classes
        lua_newtable(L); // its metatable
        lua_pushcfunction(L, safe<classes_index>);
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
        lua_setglobal(L, "classes");

        set_global(L, "new", safe<g_new>);
        set_global(L, "obj", safe<g_obj>);
        set_global(L, "keep", safe<g_keep>);
        set_global(L, "call", safe<g_call>);
        set_global(L, "clock", safe<g_clock>);
        set_global(L, "print", safe<g_print>);

        lua_sethook(L, timeout_hook, LUA_MASKCOUNT, 10000);

        auto failure = [&](const char *stage) {
            std::string msg = std::string(stage) + ": ";
            if (const char *e = lua_tostring(L, -1)) {
                msg += e;
            }
            if (!run.output.empty()) {
                msg += "\n--- output before the error ---\n" + run.output;
            }
            return dyn::Error(msg);
        };

        lua_pushcfunction(L, traceback);
        const int handler = lua_gettop(L);
        if (luaL_loadbufferx(L, code.data(), code.size(), "=script", "t") != LUA_OK) {
            throw failure("syntax error");
        }
        if (lua_pcall(L, 0, LUA_MULTRET, handler) != LUA_OK) {
            throw failure("script error");
        }

        // Whatever the script returned, after the handler.
        const int nret   = lua_gettop(L) - handler;
        json      result = nullptr;
        if (nret == 1) {
            result = to_json(L, handler + 1, run);
        } else if (nret > 1) {
            result = json::array();
            for (int i = 1; i <= nret; ++i) {
                result.push_back(to_json(L, handler + i, run));
            }
        }

        const std::chrono::duration<double> elapsed = Clock::now() - run.start;
        json out = {{"result", result}, {"elapsed_s", elapsed.count()}};
        if (!run.output.empty()) {
            out["output"] = run.output;
        }
        if (run.output_truncated) {
            out["output_truncated"] = true;
        }
        return out;
    }

    inline void enable_lua(Server &server, LuaOptions opts) {
        json def = {
            {"name", "run_script"},
            {"description",
             "Run a Lua 5.4 script inside the server, next to the live C++ objects — use it "
             "for loops, searches and anything that would otherwise take many tool calls. "
             "API: obj(\"handle\") fetches a live object; new(\"Class\", args...) or "
             "classes.Class(args...) constructs one; o.field reads, o.field = v writes "
             "(ranges and read-only enforced); o:method(args...) calls a method; "
             "classes.Class.staticMethod(args...) calls a static one; call(\"function\", "
             "args...) a free function; keep(\"name\", o) publishes an object as a handle for "
             "later tool calls; print(...) is captured into `output`; clock() is seconds "
             "since the script started. Enums are strings (enumerator names); vectors are "
             "Lua tables, passed whole. The script's return value is the result (objects "
             "become handles, long arrays are summarized). Errors raise Lua errors "
             "(catchable with pcall). Sandboxed: no io/os/require."},
            {"inputSchema",
             {{"type", "object"},
              {"properties",
               {{"code", {{"type", "string"}, {"description", "Lua source"}}},
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
            return run_lua(server, a["code"].get<std::string>(), t, opts);
        });
    }

} // namespace rosetta::mcp
