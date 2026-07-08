# Extending Rosetta with a new backend (Lua example)

Rosetta ships several backends — `python`, `node`, `rest`, `wasm`, `...` — but the
generator is **open for extension**: you can teach it a new target language
without editing `rosetta::generate` or any core file. This guide adds a
`lua` backend as a worked example.

## The mental model: two layers

A backend is two independent pieces that run at two different times:

| Layer | File you write | Runs when | Uses reflection? |
|---|---|---|---|
| **Runtime visitor** | `rosetta/visitors/lua_visitor.h` | when the *generated* binding is compiled | **yes** — walks the class |
| **Generation backend** | a `rosetta::Backend` subclass | when `rosetta_gen`'s driver runs | **no** — pure text templating |

The split exists because `rosetta::generate<Ts...>` **erases the type pack up
front** into plain strings:

```cpp
struct GenClass { std::string name; std::string header; std::string doc; };
```

So a `Backend` never sees a C++ type — only `{name, header, doc}` per class.
All the real reflection (enumerating fields/methods/ctors) happens later, in
your **visitor**, when the generated `auto_lua.cpp` is compiled. Keep the two
layers straight and everything else follows.

```
manifest.json ─(rosetta_gen)→ gen/ ─(build driver)→ ./generator ─(run)→ bindings/lua/
                                                       │                    auto_lua.cpp  ← includes lua_visitor.h
                                                       │                    CMakeLists.txt
                                              LuaBackend::emit()            (compiling auto_lua.cpp runs the walk)
```

---

## Layer 1 — the runtime visitor (`lua_visitor.h`)

This is where binding actually happens. It implements the **walk visitor
concept** (see [`walk.h`](../include/rosetta/walk.h)): three required member
templates plus one optional, each receiving a `std::meta::info` of the
member and its annotation pack.

```cpp
// include/rosetta/visitors/lua_visitor.h
#pragma once
#include <experimental/meta>
#include <rosetta/walk.h>
#include <sol/sol.hpp>   // example: bind via the sol2 Lua wrapper
#include <string>

namespace rosetta {

    template <typename T> struct LuaVisitor {
        sol::usertype<T> &ut;

        // A data member. `Fld` is the reflected field; `Anns...` is its
        // annotation pack (rosetta::doc / readonly / range / ...).
        template <std::meta::info Fld, auto... Anns> void field(const char *name) {
            using F = [:std::meta::type_of(Fld):];
            if constexpr (ann::has<readonly>(Anns...)) {
                ut[name] = sol::readonly_property([](const T &s) -> F { return s.[:Fld:]; });
            } else {
                ut[name] = sol::property([](const T &s) -> F { return s.[:Fld:]; },
                                         [](T &s, F v) { s.[:Fld:] = v; });
            }
        }

        // An instance method.
        template <std::meta::info Fn, auto... Anns> void method_instance(const char *name) {
            ut[name] = &[:Fn:];
        }

        // A static method.
        template <std::meta::info Fn, auto... Anns> void method_static(const char *name) {
            ut[name] = &[:Fn:];
        }

        // OPTIONAL: constructors. Omit the method entirely and the walk skips
        // it (it is detected with `requires`). See the pybind/embind visitors
        // for how to splice the parameter pack into a real ctor call.
        // template <std::meta::info Ctor, auto... Anns> void constructor() { ... }
    };

    // Entry point the generated code calls, one per class.
    template <typename T> void bind_lua(sol::state &lua, const char *name) {
        sol::usertype<T> ut = lua.new_usertype<T>(name);
        LuaVisitor<T> v{ut};
        walk<T>(v);
    }

} // namespace rosetta
```

The annotation helpers you'll use inside the visitor:

- `rosetta::ann::has<readonly>(Anns...)` — is annotation `A` present?
- `rosetta::ann::get_or<doc>(doc{""}, Anns...)` — fetch annotation `A` or a fallback.

And the reflection splices: `[:std::meta::type_of(Fld):]` for a field's type,
`obj.[:Fld:]` to access it, `&[:Fn:]` for a member-function pointer.

> **Tip:** if Lua (like N-API/embind) can't marshal some parameter or return
> type, mirror the `napi_supported` / `rest_supported` pattern — a `consteval`
> predicate plus an `if constexpr` guard in `field`/`method_*` — so unbindable
> members are *skipped* instead of failing the build.

---

## Layer 2 — the generation backend (`LuaBackend`)

This scaffolds the per-target project. It implements `rosetta::Backend`:

```cpp
struct Backend {
    virtual ~Backend() = default;
    virtual void emit(const GenContext &) const = 0;
};
```

with everything it needs handed over in `GenContext`:

| `GenContext` field | Meaning |
|---|---|
| `out_dir` | root of the generated tree (`bindings/`) |
| `lib` | this target's module/library name (from the manifest `targets` entry) |
| `classes` | `std::vector<GenClass>` — `{name, header, doc}` for every class |
| `user_include` | absolute dir holding the class headers |
| `rosetta_include` | absolute path to rosetta's `include/` |

A minimal `LuaBackend` writes one `.cpp` that includes the visitor and calls
`bind_lua<T>` per class, plus a `CMakeLists.txt`:

```cpp
#include <rosetta/generate.h>
#include <filesystem>
#include <fstream>

struct LuaBackend : rosetta::Backend {
    static void write(const std::filesystem::path &p, const std::string &s) {
        std::filesystem::create_directories(p.parent_path());
        std::ofstream(p) << s;
    }

    void emit(const rosetta::GenContext &c) const override {
        std::string includes, binds;
        for (const auto &k : c.classes) {
            includes += "#include \"" + k.header + "\"\n";
            binds    += "    rosetta::bind_lua<" + k.name + ">(lua, \"" + k.name + "\");\n";
        }

        const auto dir = c.out_dir / "lua";
        write(dir / "auto_lua.cpp",
              "// Generated by rosetta — do not edit by hand.\n" + includes +
              "#include <rosetta/visitors/lua_visitor.h>\n"
              "#include <sol/sol.hpp>\n\n"
              "extern \"C\" int luaopen_" + c.lib + "(lua_State *L) {\n"
              "    sol::state_view lua(L);\n" + binds +
              "    return 0;\n}\n");

        write(dir / "CMakeLists.txt",
              "cmake_minimum_required(VERSION 3.28)\n"
              "project(" + c.lib + "_binding CXX)\n"
              "# ... find_package(Lua) / sol2, add_library(" + c.lib + " MODULE auto_lua.cpp),\n"
              "#     target_include_directories(... " + c.user_include + " " + c.rosetta_include + "),\n"
              "#     and the -freflection flags, mirroring bindings/python/CMakeLists.txt\n");
    }
};
```

The built-in backends — a declaration header in
[`include/rosetta/backends/`](../include/rosetta/backends/) (`python_backend.h`,
`node_backend.h`, `rest_backend.h`, `wasm_backend.h`) with its implementation
alongside in [`include/rosetta/backends/inline/`](../include/rosetta/backends/inline/)
(`python_backend.hxx`, …) — are the reference for the CMake flags
(`-freflection -freflection-latest -fexperimental-library -fannotation-attributes`,
the clang-p2996 link line, etc.).

---

## Registering the backend

`rosetta::generate` dispatches each target through a runtime registry keyed by
the manifest `lang` string:

```cpp
std::map<std::string, std::shared_ptr<Backend>> &backend_registry();
void register_backend(std::string lang, std::shared_ptr<Backend> backend);
```

Register your backend at static-init time with the `BackendRegistrar` helper.
Put both layers in one self-contained plugin translation unit:

```cpp
// lua_plugin.cpp  — your plugin, owned entirely by you
#include "LuaBackend.h"      // the Backend above (or define it inline here)

static const rosetta::BackendRegistrar
    lua_reg{"lua", std::make_shared<LuaBackend>()};
```

Because the registrar runs before `main`, the `"lua"` backend is present by
the time the driver calls `generate` — **no edit to `generate` required**. An
unregistered target is simply warned about and skipped.

---

## Wiring it into a project

Point the manifest at your plugin with the optional `plugins` field, and add a
`lua` target. `rosetta_gen` compiles each listed source into the generated
driver's `add_executable`, and resolves the paths relative to the manifest.

```json
{
  "user_include": "./geom",
  "rosetta_include": "../../include",
  "plugins": ["lua_plugin.cpp"],
  "targets": [
    { "lang": "python", "name": "pygeom" },
    { "lang": "lua",    "name": "mygeom" }
  ],
  "classes": [
    { "name": "Model", "header": "Model.h" },
    { "header": "Point.h" }
  ]
}
```

Then the usual flow (see [`QUICKSTART.md`](./QUICKSTART.md)):

```bash
../../bin/rosetta_gen manifest.json gen   # gen/ + your plugin baked into its CMakeLists
cmake -S gen -B gen/build && cmake --build gen/build
./generator bindings                # emits bindings/python/ AND bindings/lua/
```

`bindings/lua/` now contains `auto_lua.cpp` (one `bind_lua<T>` per class) and a
`CMakeLists.txt` ready to compile against your `lua_visitor.h`.

---

## API reference (quick recap)

**Generation layer** (`<rosetta/generate.h>`):

- `struct GenClass { std::string name, header, doc; };`
- `struct GenContext { std::filesystem::path out_dir; std::string lib; std::vector<GenClass> classes; std::string user_include, rosetta_include; };`
- `struct Backend { virtual void emit(const GenContext&) const = 0; };`
- `void register_backend(std::string lang, std::shared_ptr<Backend>);`
- `struct BackendRegistrar { BackendRegistrar(std::string lang, std::shared_ptr<Backend>); };`
- `std::map<std::string, std::shared_ptr<Backend>> &backend_registry();`

**Runtime layer** (`<rosetta/walk.h>`) — the visitor concept:

- `v.template field<Fld, Anns...>(const char *name);`
- `v.template method_instance<Fn, Anns...>(const char *name);`
- `v.template method_static<Fn, Anns...>(const char *name);`
- `v.template constructor<Ctor, Anns...>();` — **optional**, called only if defined
- `rosetta::ann::has<A>(Anns...)` / `rosetta::ann::get_or<A>(fallback, Anns...)`

See [`GENERATE.md`](./GENERATE.md) for the manifest schema and the two-tool
pipeline, and the existing visitors in
[`include/rosetta/visitors/`](../include/rosetta/visitors/) as templates to
copy from.
