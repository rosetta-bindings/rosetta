# Scriptable object model — bind the reflection API itself

## Introduction
Normally rosetta generates one binding per C++ class per language, so every time your library grows a class you regenerate and recompile everything; this example does the opposite — it points rosetta at rosetta's own reflection API (`rosetta::script`) and binds that once, so your library's classes stop being bound types and become data that scripts reach by string name at runtime (`meta.create("scene::Mesh")`, `m.set("opacity", 0.5)`, `m.call("describe", [])`, `k.fields()`), which is why nothing in manifest.json or the generated code ever mentions `scene::Mesh` — the drivers name it once, as a string. The power is that one binding then serves any rosetta-dynamic library and keeps working as that library grows, and because the annotations travel with the metadata (ranges, choices, docstrings, enum names) and are enforced in the C++ core rather than re-implemented per backend, you can write a property-panel or menu generator once, in the language whose UI it builds — Python for Tk, JS for web, Lua for a game editor — instead of hand-writing a C++ walker per toolkit, which is exactly the five duplicated walkers examples/dynamic needs and this one replaces (`drive.py`'s build_panel_spec is `qt/propertypanel.h`, in ~20 lines of Python). The cost is that every access is a name lookup plus overload scoring, so it's right for menus, panels and glue and wrong for an inner loop.

Usage is one command, from `examples/scriptable-model`:

```sh
./run.sh          # generate + build, then the Python driver
./run.sh lua      # ...the Lua one
./run.sh node     # ...the Node one
./run.sh qt       # ...a live Qt property editor, also in Python (drive_qt.py)
```

One caveat worth knowing before you run it: stage 1 lives in examples/dynamic, not here — that's where the scene library's metadata tables get generated, and they aren't checked in, so a fresh clone has to build that example first (run.sh handles it; the step-by-step section of the README spells it out). If you want the same idea without the two-example indirection, minimal/ rebuilds it from scratch on a 25-line library that has never heard of rosetta.

## This example

`examples/dynamic` shows the metadata driving a GUI. Every consumer there is C++: `interp.h`, `qt/propertypanel.h`, `qt/mainwindow.h`, and — elsewhere in the tree — `runtime/imgui.h` and `visitors/qml_reflected_object.h`. Five hand-written walkers over the same tables, one per toolkit.

## Build

One-time bootstrap: fetch `rosetta` into `extern/` and build `rosetta_gen`:

This example does the Graphite/GOM move instead: it runs rosetta on **rosetta's own reflection API**, so the walker can be written in Python, Lua or JS. One UI generator per toolkit, written once, in the language whose UI it builds.

The machinery ships in `include/rosetta`; this directory is only the manifest that wires it to a library, plus three drivers that prove it works.

```
include/rosetta/script.h                 the bindable facade over dynamic.h
include/rosetta/inline/script.hxx        its bodies
include/rosetta/runtime/script_casters.h umbrella: native values, per language
include/rosetta/runtime/script/{python,lua,node,wasm}.h   one caster each
include/rosetta/presets/scriptable.json  the binding surface, as manifest data

examples/scriptable-model/manifest.json  the whole of the per-project work
examples/scriptable-model/run.sh         both stages, then a driver
examples/scriptable-model/drive.{py,lua,js}
examples/scriptable-model/drive_qt.py    the same dispatch, wired to real widgets
examples/scriptable-model/minimal/       the same, from scratch, on a 25-line
                                         library that never heard of rosetta
```

```bash
./run.sh          # both stages, then the Python driver
./run.sh lua      # ...the Lua one
./run.sh node     # ...the Node one
./run.sh qt       # ...the Qt property editor (pip install PyQt6)
./run.sh clean
```

## Building it step by step

`run.sh` above is a convenience. Here is every command it runs, in order.
Nothing is hidden: two generation stages, then one compile per backend.

For *why* the process has these steps at all — what reflection forces, and why
your library gets read twice — see [docs/PIPELINE.md](../../docs/PIPELINE.md).

**Each step says where you must be.** Paths below are written for a checkout at
`<rosetta>`, and every `cd` is absolute so a step can be run on its own — no
step assumes you are still standing where the previous one left you.

| step | run it from |
|---|---|
| 0. build the generator tool | `<rosetta>` (the repo root) |
| 1. scene library → tables | `<rosetta>/examples/dynamic` |
| 2. reflection API → projects | `<rosetta>/examples/scriptable-model` |
| 3. compile a backend | `<rosetta>/examples/scriptable-model` |
| 4. run a driver | `<rosetta>/examples/scriptable-model` |

### 0. The generator tool (once per checkout)

📁 **`<rosetta>`** — the repo root.

Stock compiler: `rosetta_gen` only parses JSON and templates text. Skip this if
`bin/rosetta_gen` already exists.

```bash
cd <rosetta>
cmake -G Ninja -S tools/rosetta_gen -B tools/rosetta_gen/build
cmake --build tools/rosetta_gen/build          # -> <rosetta>/bin/rosetta_gen
```

### 1. The scene library → metadata tables

📁 **`<rosetta>/examples/dynamic`** — *not* this example's directory.

This stage lives in **another example**, and that is the point: this one binds
rosetta's reflection API and never names a scene type. It produces
`auto_dynamic.h` and `auto_dynamic.cpp`, the entire interface between the two
folders — and they are **not checked in**, so a fresh clone must run this.

```bash
cd <rosetta>/examples/dynamic
../../bin/rosetta_gen manifest.json gen        # emit the reflection driver project
cmake -S gen -B gen/build                      # needs clang-p2996 — add
                                               #   -DCLANG_P2996_ROOT=/path/to/clang-p2996/build
                                               #   if it is not at the default location
cmake --build gen/build -j
./generator bindings                           # -> bindings/dynamic/auto_dynamic.{h,cpp}
```

`./generator` is dropped in the current directory, so this must be run from
`examples/dynamic` and nowhere else.

### 2. The reflection API → binding projects

📁 **`<rosetta>/examples/scriptable-model`** — this example's directory, and
where you stay for steps 3 and 4.

The same three commands, this time reading this directory's `manifest.json`.
The emitted driver is named after the directory (`scriptable_model.cpp`)
because the manifest sets no `generator_name`.

```bash
cd <rosetta>/examples/scriptable-model
../../bin/rosetta_gen manifest.json gen
cmake -S gen -B gen/build
cmake --build gen/build -j
./generator bindings                           # -> bindings/{python,lua,node,wasm,typescript,markdown}
```

At this point `bindings/coverage.json` already exists: a machine-readable
account of what bound and what did not, before anything is compiled.

### 3. Compile the backends

📁 **`<rosetta>/examples/scriptable-model`** (unchanged from step 2).

**Stock compiler from here on** — nothing reflection-flavoured is left in the
generated code. Build only the ones you want; they are independent projects.

```bash
cd <rosetta>/examples/scriptable-model

# python
cmake -S bindings/python -B bindings/python/build
cmake --build bindings/python/build -j         # -> bindings/python/rosetta_meta.*.so

# lua
cmake -S bindings/lua -B bindings/lua/build
cmake --build bindings/lua/build -j            # -> bindings/lua/rosetta_meta.so

# node  (cmake-js, driven by npm — these two run INSIDE bindings/node)
(cd bindings/node && npm install && npm run build)
                                               # -> bindings/node/rosetta_meta.node

# wasm  (needs an activated emsdk)
emcmake cmake -S bindings/wasm -B bindings/wasm/build
cmake --build bindings/wasm/build -j
```

`typescript` and `markdown` emit text only — there is nothing to compile.

### 4. Run the drivers

📁 **`<rosetta>/examples/scriptable-model`** for Python and Node;
`bindings/lua` for Lua, because `require` searches the current directory.

```bash
cd <rosetta>/examples/scriptable-model
PYTHONPATH=bindings/python python3 drive.py
node drive.js
(cd bindings/lua && lua ../../drive.lua)
python3 drive_qt.py            # the Qt editor; puts bindings/python on sys.path itself
```

Both generation stages need clang-p2996; everything from step 3 on is a stock
toolchain.

## Why a facade

`rosetta::dyn` cannot be fed to the backends as-is. Three shapes block it:

| in `dynamic.h` | why it blocks | in `script.h` |
|---|---|---|
| `MetaField *fields; size_t n_fields` | pointer-and-count, so vector marshalling never fires | `std::vector<FieldInfo> fields()` |
| `const MetaClass *`, `const TypeDesc *` | raw pointer to an unbound type — the gate skips it | value-semantics handles, one pointer each |
| `using Thunk = Any (*)(...)` | function pointers marshal nowhere | ordinary `get` / `set` / `call` methods |

No metadata is copied: every handle wraps one `const` pointer into `.rodata`. Nothing is named `Class` / `Type` / `Object` / `Method`, because those collide in the target languages (`java.lang.Class`, JS `Object`, Lua's `type`).

## Why one hand-written piece, and how little of it there is

`Value` is type erasure: one C++ type standing for "whatever the host language just handed us". Reflection can describe it — it cannot know that a Python `float`, a Lua number and a JS `Number` should all become the same box. That mapping is per-language knowledge, so it is written per language.

But only *half* of a caster is per-language. **Reading** a host value is irreducible: only Python knows what `PyFloat_Check` means. **Writing** one is the same shape everywhere, and lives once in `rosetta::script::visit()`:

```cpp
template <class Sink> decltype(auto) visit(const Value &v, Sink &&sink);
```

Each caster supplies a sink of nine small methods — `on_none`, `on_bool`, `on_int`, `on_real`, `on_string`, `on_enum`, `on_list`, `on_object`, `on_unknown` — and nothing else. So no two casters can drift apart on whether an enum crosses as its integer, or on what a `Kind::unknown` value renders as. Adding Julia or C# means writing a sink and a reader, not rediscovering the policy.

| target | file | customization point | status |
|---|---|---|---|
| python | `script/python.h` | `pybind11::detail::type_caster<Value>` | ✅ verified |
| lua | `script/lua.h` | `sol::stack::unqualified_{getter,pusher,checker}` | ✅ verified |
| node | `script/node.h` | `rosetta::to_napi` / `from_napi` specializations | ✅ verified |
| wasm | `script/wasm.h` | `emscripten::internal::BindingType<Value>` | ⚠ written, never compiled — no emsdk here. Opt in with `"compile_definitions": ["ROSETTA_EMBIND_VALUE_CASTER"]` and treat the first build as the review. |

Without them, scripts write the boxing by hand:

```python
m.set("opacity", meta.Value.number(0.5))
m.get("opacity").value().as_number()
```

With them, values cross natively in both directions:

```python
m.set("opacity", 0.5)
m.get("opacity").value()          # -> 0.5
```

### How they get in

The preset adds one line to `module_init`, so a manifest never spells it out:

```json
"module_init": { "headers": ["rosetta/runtime/script_casters.h"] }
```

`module_init` is the only hook that emits an `#include` **after** the framework header and **before** the bound classes — exactly the window a caster needs. `module_init.headers` is global rather than per-target, so that include reaches every backend; each file therefore guards its whole body on a macro only its own target defines (`PYBIND11_VERSION_MAJOR`, `SOL_VERSION_STRING`, `NAPI_VERSION`, `__EMSCRIPTEN__`). The three that aren't the current target expand to nothing. Name one file directly if you'd rather be explicit.

`Value` must stay in the bound `classes` list — which is why the preset lists it. The type gate decides bindability at generation time and cannot know a caster will exist, so dropping `Value` would silently skip every member that mentions it — `Outcome::value`, `Instance::set`, `Instance::call`, the lot. The cost is one vestigial `Value` type per module that nothing ever produces.

## What it produces

Measured, not projected — `bindings/coverage.json` after a build:

```
python  bound 108  skipped 1
lua     bound 108  skipped 1
node    bound 108  skipped 1
wasm    bound 108  skipped 1
```

The one skip is `Value::raw()`, returning `const dyn::Any &` — the deliberate C++ escape hatch, unmarshalable by construction and unneeded from a script.

The same session, three languages, `scene::Mesh` named nowhere:

Python:
```python                          # drive.py
m.set("opacity", 0.5)             # -> 0.5
m.set("weights", [0.25, 0.5, 1])  # -> [0.25, 0.5, 1.0]
m.set("opacity", 99)              # -> "opacity = 99 is outside [0, 1]"
cube.call("at", [0, 1]).value()   # -> 2.0
```
Lua:
```lua                             -- drive.lua
m:set("opacity", 0.5)
m:set("weights", { 0.25, 0.5, 1 })
cube:call("at", { 0, 1 }):value()
```
JavaScript:
```javascript                      // drive.js
m.set("opacity", 0.5);
m.set("weights", [0.25, 0.5, 1]);
cube.call("at", [0, 1]).value();
```

Three things are worth naming:

- **Range enforcement, choices and readonly reach every language** — annotations are enforced once in the core rather than re-implemented per backend.
- **Both overloads of `at()` survive**, and a failed call names every candidate and why it lost. The name-keyed backends (lua, node, wasm, C#, Java, REST) bind the first overload and drop the siblings at generation time ([COVERAGE.md](../../docs/COVERAGE.md)); through this module they don't, because `dyn::resolve` scores the whole set at call time.

  ```
  Mesh::at(std::string) matched no overload of 2:
    at(int) — argument 1 ("nope") is not a int
    at(int, int) — takes 2 argument(s), 1 given
  ```
- **`onProgress` is described, not deleted** — a UI greys it out and says why (`a std::function parameter needs a foreign-callable adapter`).

And the thing the exercise is for — `qt/propertypanel.h`'s widget dispatch, in the language of the toolkit binding (`build_panel_spec` in `drive.py`):

```python
for f in inst.cls().fields():
    if not f.readable():        continue
    t = f.type()
    if   f.choices():           w = ("combo",    f.choices())
    elif t.kind() == "enum":    w = ("combo",    t.enumerator_names())
    elif f.has_range():         w = ("slider",   (f.range_min(), f.range_max()))
    elif t.kind() == "boolean": w = ("checkbox", None)
    else:                       w = ("text",     None)
    rows.append({"label": f.name(), "tooltip": f.doc(), "widget": w,
                 "value": inst.get(f.name()).value(), "enabled": not f.readonly()})
```

Ten lines, editable without recompiling anything, and `value` is a real Python object rather than a box.

## The same thing, with real widgets

[`drive_qt.py`](drive_qt.py) (`./run.sh qt`, needs `pip install PyQt6`) replaces the descriptors above with a live Qt editor — what [`examples/dynamic/qt/`](../dynamic/qt/) hand-wrote in C++, here in ~480 lines of Python that no C++ programmer had to touch:

| what you see | where it comes from |
|---|---|
| row label | the `label` annotation, else `f.name()` |
| slider / checkbox / colour swatch / radio row / text field | the `widget` annotation, else the field's `kind()` |
| slider bounds | `has_range()` / `range_min()` / `range_max()` |
| combo entries | `choices()`, or a `TypeInfo`'s `enumerator_names()` for an enum |
| tooltip | `doc()` |
| greyed out | `readonly()` / `writable()`, and `readable()` for what the type gate could not marshal |
| `Origin` sub-panel | the field's `kind() == "object"` — the `Instance` it returns pins its parent, so those spin boxes write straight into the mesh |
| Describe / Reset / Subdivide | methods carrying a `button` annotation |
| the **New** menu | `classes()`, plus every static method whose `ret().kind()` is `"object"`; `sphere(rings, segments)` gets an argument dialog built from `params()` |
| the **Call** menu | every instance method — including *both* `at()` overloads, numbered from `overload_index()` / `overload_count()`, and `onProgress` listed **disabled** with its `skip_reason()` as the tooltip |
| the mesh itself | `positions()` and `triangles()`, invoked **by name** — the geometry is `private` in `scene.h`, so reflection never sees the arrays, only the accessors |

Nothing in the file names a scene type, a field or a method. Point the manifest at a different library and it still runs: it is a generic editor for anything the `dynamic` backend has described — pick `scene::Vec3` from the **New** menu and you get its three spin boxes, no buttons (nothing annotated one) and "no geometry" in the viewport, from the same code path.

Two things worth doing while it is open. Drag the viewport: the `Spin` slider follows, because the drag writes through `inst.set("spin", …)` and wraps into the range the *annotation* declared — the panel never hard-codes 360. And notice that the sliders cannot produce an out-of-range value at all: their bounds *are* `range_min()`/`range_max()`, so the check the core would perform (`opacity = 99 is outside [0, 1]`, which is what `drive.py` prints and what a script hitting the same field gets) is one the widget can no longer fail. That is the argument in miniature — the constraint is declared once, next to the field, and every consumer in every language derives its behaviour from it.

## Using it on your own library

The facade is generic; the *tables* are not. See [`minimal/`](minimal/) for the whole thing done from scratch on a 25-line stock-C++ library that has never heard of rosetta — it exists so this claim can be checked rather than believed.

**Three files.** Two of them generated:

| file | where it comes from |
|---|---|
| your header(s) | yours — `auto_dynamic.cpp` `#include`s them |
| `auto_dynamic.h` | generated — declares `<lib>::register_all()` |
| `auto_dynamic.cpp` | generated — the tables |

Plus your `.cpp` / `.a` if the library is not header-only (`user_sources` or `user_lib`). The generated pair comes from a six-line manifest with one target:

```json
{
    "user_include": ".",
    "rosetta_include": "/path/to/rosetta/include",
    "generator_name": "bank_dynamic_gen",
    "module_name": "bank",
    "targets": [{ "lang": "dynamic", "name": "bank" }],
    "classes": [{ "name": "bank::Account", "header": "bank.h" }]
}
```

**Three manifest keys**, plus a preset and the targets. That is the whole manifest:

```json
{
    "preset": "scriptable",
    "rosetta_include": "<rosetta>/include",
    "user_include": ["<your headers>", "<dir of auto_dynamic.h>"],
    "user_sources": ["<dir>/auto_dynamic.cpp"],
    "module_init":  { "headers": ["auto_dynamic.h"], "statements": ["bank::register_all()"] },
    "targets": ["python", "lua"]
}
```

`"preset": "scriptable"` pulls the binding surface out of the manifest entirely — the twelve handle classes, the eight registry functions, and the caster include all come from [`include/rosetta/presets/scriptable.json`](../../include/rosetta/presets/scriptable.json), which is where they belong: they describe `rosetta/script.h`, not your project. The preset also puts `rosetta_include` on the target's include path, since the bound headers are rosetta's own. See [Presets](../../docs/MANIFEST.md#presets-preset).

No class list to maintain, no regeneration when your library grows a class.

**One constraint.** `auto_dynamic.cpp` includes your header, so a header carrying **inline** annotations (`[[ = rosetta::doc{...} ]]`) drags `<experimental/meta>` into it and needs the C++26 toolchain to build the *target*, not just the generator. Put annotations **out of line** in a `.ann.json` side-car — as `examples/dynamic/scene.h` does — if you want them and a stock-compiler build.

The next step, not done here, is dropping even the three keys: a `load(path)` function that `dlopen`s a shared library whose `register_*` self-registers, giving one prebuilt `rosetta_meta` wheel that reflects *any* rosetta-dynamic
library.
