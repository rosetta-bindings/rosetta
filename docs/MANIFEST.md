# `manifest.json` — the rosetta manifest

The **manifest** is the single hand-written file that drives rosetta. You point it at your existing C++ headers, list the languages you want bindings for, and `rosetta_gen` does the rest. Your class definitions are never modified.

It answers three questions for the framework:

1. **Where** are your headers and where is rosetta's `include/`?
2. **What** classes / free functions should be bound?
3. **Which** language backends (targets) should be emitted?

Everything else — fields, methods, constructors, enums, inheritance — is discovered by C++26 reflection from the headers themselves. You declare *__what__* to bind, never *__how__*.

---

## Where it fits

```
manifest.json ──► rosetta_gen ──► generated/  ──cmake──► <generator_name>
                  (framework)     bindings.h              (project tool)
                                  <generator_name>.cpp        │
                                  CMakeLists.txt              run
                                                              ▼
                                                      output/python  node  wasm …
                                                      (per-backend project trees)
```

`rosetta_gen` reads the manifest and emits a project-specific generator; that generator, when run, emits one self-contained CMake project per target. Paths inside the manifest are resolved **relative to the manifest file** — move the file and you must re-run `rosetta_gen`.

---

## Minimal example

Start from a blank binding project scaffolded by `tools/rosetta_init.py` — it writes a `rosetta/` folder next to your library (`manifest.json` skeleton, a bootstrap `CMakeLists.txt` that fetches rosetta into `extern/` and builds `rosetta_gen`, `.gitignore`, `README.md`).

Download the script for scaffolding: [**rosetta_init.py**](https://github.com/rosetta-bindings/rosetta/blob/main/tools/rosetta_init.py), then:

```bash
# 1. scaffold an empty starting project inside (or next to) your library
python3 rosetta_init.py --dir my_lib/rosetta --name my_lib
```

```sh
cd my_lib/rosetta
```

Fill in the generated `manifest.json` — the scaffold leaves `classes` empty; add the types you want bound:

```json
{
  "user_include": ["./include"],
  "user_sources": ["./src/*.cxx"],
  "rosetta_include": "./extern/rosetta/include",
  "generator_name": "my_lib_gen",
  "targets": ["python-expanded", "node-expanded", "rest-expanded", "wasm-expanded"],
  "classes": [
    { "name": "Person", "header": "person.h" }
  ]
}
```

Build & run:

```bash
# 2. one-time bootstrap: fetch rosetta into extern/ and build rosetta_gen
#    (binary lands in extern/rosetta/bin)
cmake -B build && cmake --build build

# 3. the whole pipeline in one command: emit + build + run the generator,
#    then compile every backend the manifest declares
./extern/rosetta/bin/rosetta_gen --build manifest.json
```

`--build --help` lists the options (`--only`/`--skip` backends, `--jobs`, `--fresh`, …), and `rosetta_gen --clean manifest.json` removes everything it generated. Every mode and option of the tool — including running the steps `--build` automates by hand — is documented in [ROSETTA_GEN.md](ROSETTA_GEN.md).

---

## Top-level fields

| Field | Required | Default | Meaning |
|---|:---:|---|---|
| `user_include` | ✅ | — | Directory holding your class headers — **or an array of directories** when they live in several places. Each entry is relative to the manifest, or absolute, and resolved to an absolute path. See [Multiple include directories](#multiple-include-directories). |
| `rosetta_include` | ✅ | — | Path to rosetta's `include/` directory. Same resolution rules. |
| `generator_name` | ✅ | — | CMake target / binary name of the generated project tool. `"my_person_gen"` ⇒ `my_person_gen.cpp` and a `my_person_gen` binary. |
| `module_name` | — | `generator_name` | Default binding module name, used by any **shorthand** (bare-string) target. |
| `targets` | ✅ | — | The language backends to emit. See [Targets](#targets). |
| `classes` | ✅ | — | The classes / structs / enums to bind. See [Classes](#classes). |
| `functions` | — | `[]` | Free (non-member) functions to bind. See [Functions](#functions). |
| `namespace` | — | — | Default C++ namespace for `classes` / `functions` / `extensions` names carrying no `::` of their own. See [Shared defaults](#shared-defaults-namespace-header_dir). |
| `header_dir` | — | — | Directory fragment prepended to every `classes` / `functions` / `extensions` header. See [Shared defaults](#shared-defaults-namespace-header_dir). |
| `sequences` | — | `[]` | Foreign sequence containers (qualified template names, one type parameter — `"GEO::vector"`) that marshal like `std::vector<T>`. See [Foreign sequence containers](#foreign-sequence-containers-sequences). |
| `user_lib` | — | — | Link the generated bindings against a pre-built external library. See [Linking an external library](#linking-an-external-library-user_lib). |
| `user_sources` | — | `[]` | List of user `.cpp` (or `.c`) files compiled directly into every generated binding target. See [Compiling user sources](#compiling-user-sources-user_sources). |
| `compile_definitions` | — | `[]` | Preprocessor definitions (`"NAME"` or `"NAME=VALUE"`) applied to the generator driver and every compiled binding target. See [Preprocessor definitions](#preprocessor-definitions-compile_definitions). |
| `build_type` | — | — | Default `CMAKE_BUILD_TYPE` baked into every compiled backend's generated `CMakeLists.txt` (`Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`). See [Build type & optimization](#build-type--optimization-build_type-optimization). |
| `optimization` | — | — | Explicit optimization flag (`-O0`…`-O3`, `-Os`, `-Oz`, `-Og`, `-Ofast`) applied to every compiled backend, overriding the build type's own `-O` level. See [Build type & optimization](#build-type--optimization-build_type-optimization). |
| `plugins` | — | `[]` | Extra `.cpp` sources to compile into the generator driver (e.g. a custom backend). Paths relative to the manifest. |
| `qt_dir` | — | a built-in path | Qt 6 install prefix used by the `qt` / `qml` (and `-expanded`) backends. e.g. `"$ENV{HOME}/Qt/6.8.3/macos"`. |
| `cpp26_root` | — | `$ENV{HOME}/devs/c++/clang-p2996/build` | Root of the C++26 / P2996 reflection toolchain used by the *thin* backends. Moves `cpp26_cxx` / `cpp26_cc` / `cpp26_lib` together. |
| `cpp26_cxx` | — | `${cpp26_root}/bin/clang++` | C++ compiler (name or path) for the reflection toolchain. |
| `cpp26_cc` | — | `${cpp26_root}/bin/clang` | C compiler (name or path). |
| `cpp26_lib` | — | `${cpp26_root}/lib` | Directory holding the fork's `libc++` / `libc++abi` (`-L` / rpath). |

Keys beginning with `//` (e.g. `"//1"`, `"//note"`) are treated as comments and ignored — handy since JSON has no comment syntax.

---

## Classes

`classes` is an array of per-class entries. Each binds one C++ type (`class`, `struct`, or `enum`).

```json
"classes": [
  { "name": "Model", "header": "Model.h", "doc": "the model class" },
  { "header": "Point.h" },
  { "name": "space::Vector3", "header": "Vector3.h", "annotations": "Vector3.ann.json" }
]
```

| Field | Required | Default | Meaning |
|---|:---:|---|---|
| `header` | ✅ | — | Filename emitted into `#include "..."`. Resolved against `user_include`. |
| `name` | — | header basename | C++ type name, must be reachable after including `header`. May be namespace-qualified (`space::Vector3`). |
| `annotations` | — | — | Path (relative to the manifest) to an out-of-line annotation JSON side-car, baked into `bindings.h` so the header stays clean. See [OUT_OF_LINE_ANNOTATIONS](OUT_OF_LINE_ANNOTATIONS.md). |
| `doc` | — | — | A description string for the class (used by doc backends). |
| `extensions` | — | `[]` | Free functions exposed as **instance methods** of this class. See [Extension methods](#extension-methods-extensions). |
| `final` | — | `false` | Treat the class as non-overridable from the host language: **no trampoline** is generated even when it has public virtual methods (they still bind as ordinary callable methods). Also what makes a class *with* virtuals eligible as a node member-object property (`mesh.vertices` — the aliased wrap stores a `T*`, which requires the wrapped type to be `T`, not `Js_T`). |

Inheritance, multiple constructors, enums, nested user types and `std::vector` members are discovered automatically — no entry needed per base class, just list the most-derived type you want bound.

---

## Shared defaults (`namespace`, `header_dir`)

When every entry repeats the same namespace and the same header folder —

```json
"classes": [
  {"name": "stressinv::Serie",      "header": "stressinv/Serie.h"},
  {"name": "stressinv::Data",       "header": "stressinv/Data.h"},
  {"name": "stressinv::CostMetric", "header": "stressinv/cost.h"}
]
```

— factor them out with the two optional top-level defaults:

```json
"namespace": "stressinv",
"header_dir": "stressinv",
"classes": [
    {"header": "Serie.h"},
    {"header": "Data.h"},
    {"name": "CostMetric", "header": "cost.h"}
]
```

(`Serie` and `Data` also drop their `name`, since it defaults from the header stem — and the derived name is namespace-qualified too.)

Rules, applied identically to `classes`, `functions` and `extensions`:

- `namespace` qualifies an entry name only when it carries **no `::` of its own**: `"Serie"` → `stressinv::Serie`. A name containing `::` passes **verbatim** — so fully qualified spellings, nested classes (`stressinv::Model::Inner`) and sub-namespaces (`stressinv::detail::helper`) keep working unchanged, and mixing shortened and full spellings in one manifest is fine.
- A **leading `::`** pins an entry to the global namespace: `"::c_entry"` → `c_entry`. This is the escape hatch for the odd global function (e.g. `extern "C"`) in an otherwise-namespaced manifest.
- `header_dir` is prepended to **every** entry header (a `/` is inserted if missing): `"Serie.h"` → `stressinv/Serie.h`. A header living elsewhere can step out relative to it (`"../other/x.h"`), or you can keep `header_dir` unset and spell every path in full.

`rosetta_gen --init <src_dir>` factors its scanned output the same way: when every found name shares one namespace (and every header one first-level folder), the generated manifest uses these defaults instead of repeating them per entry.

### Grouped entries

One pair of top-level defaults can't cover a library whose headers live in several folders (`solvers/`, `postprocess/`, `algos/stress-inversion/`) or that uses sub-namespaces. For that, an element of `classes` or `functions` may be a **group**: an object carrying `"entries"` (a nested entry list) plus its own local defaults, instead of being an entry itself.

```json
"namespace": "arch",
"header_dir": "Arch",
"classes": [
  {"name": "Vector3", "header": "math/math.h"},

  {"header_dir": "solvers", "entries": [
    {"name": "GmresSolver", "header": "Gmres.h"},
    {"header": "ParallelSolver.h"}
  ]},

  {"header_dir": "algos", "entries": [
    {"header": "DataSuperposition.h"},
    {"namespace": "sinv", "header_dir": "stress-inversion", "entries": [
      {"header": "JointData.h"},
      {"header": "types.h", "entries": [
        {"name": "MCMCConfig"},
        {"name": "MCMCResult"}
      ]}
    ]}
  ]}
]
```

Group rules:

- A group's `header_dir` **appends below** the inherited dir: `Arch` + `solvers` ⇒ `Arch/solvers/Gmres.h`.
- A group's `namespace` **appends to** the inherited one: `arch` + `sinv` ⇒ `arch::sinv::JointData`. A leading `::` makes it absolute instead of appending.
- A group may set a `header`: the default header for entries that spell none — the natural shape for a run of classes declared by one header (`types.h` above ⇒ `arch::sinv::MCMCConfig` et al., all from `Arch/algos/stress-inversion/types.h`). Such entries need an explicit `name` (the stem fallback would give every one the same name).
- Groups **nest**, **mix freely with plain entries** in the same array, and work identically under `functions` — where a shared-header group reads especially well (a dozen shape generators all declared by `shapes.h`).
- A group cannot carry a `name`, and `//`-comment keys are ignored on groups like everywhere else.

Everything is resolved at load time, so backends and generated output are byte-identical to the fully spelled form.

Members the emitted binding could not compile are **skipped** rather than fatal: a public field whose type is a non-copyable class (e.g. a member object holding a back-reference to its owner), a method returning a reference to such a type, or a by-value parameter of one. The class still binds — as an opaque handle plus whatever members do marshal — and [extension methods (#extension-methods-extensions) fill the gaps.

---

## Extension methods (`extensions`)

Some libraries keep their real API where no binding generator can reach it: `GEO::Mesh`'s geometry lives behind public member objects with raw `double*` accessors, its I/O helpers are overloaded, its UV coordinates sit in an attribute template. Rather than hand-writing a wrapper *class*, list plain free functions — whose **first parameter is `Cls&` (or `const Cls&`)** — as `extensions` of the bound class; they appear to every backend as ordinary instance methods:

```json
"classes": [{
  "name": "GEO::Mesh", "header": "geogram/mesh/mesh.h",
  "extensions": [
    { "name": "georo::set_surface", "header": "mesh_ext.h",
      "doc": "Set vertices + triangles from flat arrays." },
    { "name": "georo::vertices",    "header": "mesh_ext.h",
      "doc": "Vertex coordinates as a flat array." }
  ]
}]
```

```py
m = geogram.Mesh()
m.set_surface(coords, triangles)   # calls georo::set_surface(m, ...)
print(len(m.vertices()) // 3)
```

The receiver is dropped from the exposed signature; the remaining parameters and the return type marshal exactly like a free function's. The method name is the function's unqualified identifier. Supported by the `python-expanded`, `nanobind-expanded`, `node-expanded`, `wasm-expanded` targets and all text backends (`typescript`, `markdown`, `html`); the thin (reflection-re-running) backends don't see them, and backends that can only emit member pointers (`qt`/`qml`/`csharp`/`java`) skip them.

---

## Foreign sequence containers (`sequences`)

Many libraries carry their bulk data in their **own vector type** — geogram's `GEO::vector<T>`, an aligned or pooled vector — and the marshalling layers only know `std::vector`. List the container template (qualified, **one type parameter**) under `sequences` and it crosses the boundary like a `std::vector` of its element:

```json
"sequences": ["GEO::vector"]
```

`rosetta_gen` emits `template <typename T> struct rosetta::is_sequence<GEO::vector<T>> : std::true_type {};` into the generated `bindings.h` (equivalently, write that specialization yourself for programmatic use — see `rosetta/sequence.h`). The container must be default-constructible with `value_type`, `size()`, `resize(n)` and `begin()`/`end()`; elements must be arithmetic, `bool`, `std::string` or a bound enum.

The opted-in backends (`python-expanded`, `nanobind-expanded`, `node-expanded`, `wasm-expanded`, `lua-expanded`, plus `typescript` declarations) marshal it **by copy through a `std::vector<element>` boundary** inside an emitted adapter — scripts pass and receive plain arrays/lists/tables. Every other backend keeps skipping the type (the IR leaves `kind` "unknown", like raw pointers and callbacks). Three consequences worth knowing:

- **Mutable `Seq&` parameters bind, input-only** — the adapter's local container is a real lvalue (geogram's `assign_points(vector<double>&, dim, steal)` works; `steal` steals from the adapter's copy, which is fine). In-place mutations are discarded, exactly like pybind's `std::vector&` casters.
- **Overload sets whose surviving IR entry is the sequence overload bind** — the adapter calls the method *by name* with concrete arguments instead of spelling the ambiguous `&T::name` member pointer. The walk keeps the **first-declared** overload, so `GEO::MeshVertices::assign_points` (sequence overload first) binds; a set whose first declaration is the raw-pointer one stays skipped.
- **Virtual methods naming the container can't be overridden script-side** (their trampoline `sig_bindable` is off — the exact spelling can't round-trip), but they still bind as callable methods.

Sequence-typed public **fields** bind as copying properties (python / nanobind / wasm / lua; node skips them).

---

## Multiple include directories

When your headers don't all live under a single root, give `user_include` an **array** of directories instead of a single string:

```json
"user_include": ["./geom", "../shared/include", "/opt/thirdparty/include"]
```

Each entry follows the same resolution rules as the single-string form (relative to the manifest, or absolute). Every directory is added to the generated bindings' include path, so a class `header` is resolved against **all** of them — the first directory that contains the file wins, exactly like a compiler's `-I` search order. The array must not be empty.

The single-string form is just the one-directory shorthand:

```json
"user_include": "./geom"          // equivalent to ["./geom"]
```

---

## Functions

`functions` binds **free (non-member)** functions without editing your headers. Each entry:

```json
"functions": [
  { "name": "transform", "header": "common.h",
    "doc": "Scale and swizzle a point into (x*2, z*3, y*4)" }
]
```

| Field | Required | Default | Meaning |
|---|:---:|---|---|
| `name` | ✅ | — | Function name. May be namespace-qualified (`api::add`). |
| `header` | ✅ | — | Header declaring it (emitted into `#include`). |
| `doc` | — | — | Optional description (free functions carry no in-source annotations). |

See [FREE_FUNCTIONS](FREE_FUNCTIONS.md) for details.

---

## Targets

`targets` lists the language backends. Each entry is **either**:

- a **bare string** — uses `module_name` (or `generator_name`) as the module name:

  ```json
  "targets": ["python", "node", "markdown"]
  ```

- an **object** `{ "lang": ..., "name": ... }` — sets a per-target module name:

  ```json
  "targets": [
    { "lang": "python", "name": "pygeom" },
    { "lang": "node",   "name": "jsgeom" },
    { "lang": "markdown" }
  ]
  ```

`name` is optional in the object form too (defaults to `module_name`). One generator emits a **single combined module per target** exposing every class.

The object form also accepts **`link_options`** — extra linker flags applied to *this target only*, emitted as `target_link_options(... PRIVATE ...)` in the generated project. Per-target (unlike `compile_definitions`) because link flags are toolchain-specific — e.g. geogram's `GEO::initialize()` mounts the host filesystem with NODEFS under Node, which needs emscripten's nodefs library on the **wasm** link line and would break a native link:

```json
"targets": [
  { "lang": "python-expanded" },
  { "lang": "wasm-expanded", "link_options": ["-lnodefs.js"] }
]
```

The flags are emitted **after** the template's own `target_link_options`, so on the wasm targets — where emcc honors the *last* occurrence of a repeated `-s` setting — they can also **override** a template default, e.g. `"-sALLOW_MEMORY_GROWTH=0"` for a fixed-size heap.

### Available `lang` values

Thin (reflection re-runs at the target's compile time — needs the C++26 toolchain to build) and **expanded** (reflection runs once on the host; the generated code builds with a stock compiler).

| `lang` | Output | Expanded variant |
|---|---|---|
| `python` | pybind11 extension module | `python-expanded` |
| `nanobind` | nanobind extension module | `nanobind-expanded` |
| `node` | N-API native addon | `node-expanded` |
| `wasm` | Emscripten / embind module | `wasm-expanded` |
| `qt` | Qt Widgets property/method inspector | `qt-expanded` |
| `imgui-expanded` | Dear ImGui inspector app (GLFW + OpenGL3, auto-fetched) | expanded only |
| `qml` | QML / QtQuick inspector | `qml-expanded` |
| `csharp` | C-ABI shared lib + P/Invoke wrappers | `csharp-expanded` |
| `java` | C-ABI + handle-backed FFM wrappers | `java-expanded` |
| `julia` | CxxWrap.jl / jlcxx shared module | `julia-expanded` (adds `std::vector` support) |
| `lua-expanded` | sol2 shared module, `require`-able (Lua 5.1–5.4 / LuaJIT) | expanded only |
| `rest` | cpp-httplib JSON server + browser client | — |
| `openapi` | OpenAPI 3.1 spec | text output |
| `json` | nlohmann (de)serialization | text output |
| `typescript` | ambient `.d.ts` declarations | text output |
| `markdown` | API reference document | text output |
| `html` | styled API reference page | text output |
| `paraview` | ParaView Server Manager plugin XML | text output |

The text-only outputs (`markdown`, `html`, `json`, `typescript`, `openapi`, `paraview`) don't compile anything, so the C++26-vs-stock distinction doesn't apply — they're produced directly.

> **Why expanded?** If your *target* compiler doesn't support reflection, use the `-expanded` variants: generate once on a C++26 / P2996 host, then ship and build the generated sources anywhere with a stock toolchain (plain Clang / GCC / MSVC, stock emsdk, stock Qt 6). The generator host still needs C++26; the target does not. Pairs naturally with out-of-line annotations so the bound headers stay stock C++ too. See [`examples/geom-expanded`](../examples/geom-expanded).

---

## Linking an external library (`user_lib`)

Use `user_lib` when your bound headers only **declare** the API and the definitions live in a separately-compiled library (`.so` / `.dylib` / `.a`). rosetta links the generated bindings against it and sets up rpath.

```json
"user_lib": {
  "name": "space",
  "dir":  "../space/bin",
  "link": "shared"
}
```

| Field | Required | Default | Meaning |
|---|:---:|---|---|
| `name` | ✅ | — | Library name (the `space` in `libspace.dylib`). |
| `dir` | ✅ | — | Directory holding the library (relative to the manifest; used for `-L` / rpath). |
| `link` | — | `"shared"` | `"shared"` (default), `"static"`, or `"dynamic"` (alias of `"shared"`). The *preferred* form, with fallback to whichever is actually built. |

`wasm` targets are **always** static — a native `.dylib` / `.so` cannot enter a wasm module. The native `python` / `node` targets honor `link`. See [`examples/dynamic-lib`](../examples/dynamic-lib).

---

## Compiling user sources (`user_sources`)

Use `user_sources` when your bound headers only **declare** the API and the definitions live in `.cpp` files you want **compiled straight into the binding** — rather than linked from a pre-built [`user_lib` (#linking-an-external-library-user_lib).

```json
"user_sources": [
  "../src/widget.cpp",
  "../src/shape.cpp"
]
```

It is always a **list of paths**, each relative to the manifest (or absolute). Every compiled backend adds them to its binding target via `target_sources(...)`, so they build with the same include path and flags as the generated binding. A single string is accepted as a one-element list.

Each entry may be a **shell glob**, expanded at generation time:

```json
"user_sources": ["./extern/pmp/src/pmp/algorithms/*.cpp"]
```

`*`, `?` and `[...]` are supported within a path component, and a component that is exactly `**` matches **zero or more directories** (bash-globstar style) — so one pattern covers a whole source tree instead of one line per subdirectory:

```json
"user_sources": ["extern/arch/src/**/*.cxx"]
```

matches `src/a.cxx` as well as `src/algos/stress-inversion/b.cxx`. Like the other wildcards, `**` does not enter hidden (dot) directories. Matches are sorted for reproducible output, and the final list is de-duplicated, so mixing a literal path with a glob that also covers it is safe. A pattern that matches nothing emits a warning and is skipped. Because globs expand when `rosetta_gen` runs, **re-run it after adding or removing matching files**.

`user_sources` and `user_lib` are independent — use either, or both. The text-only backends (`markdown`, `html`, `json`, `typescript`, `openapi`, `paraview`) compile nothing and ignore it.

Entries may also be **C sources** (`.c`) — e.g. a third-party library's vendored dependencies (zlib, rply, libMeshb, OpenNL…). When any `.c` file is listed, the generated CMakeLists calls `enable_language(C)` automatically so they compile alongside the C++ binding:

```json
"user_sources": [
  "./geogram/src/lib/geogram/mesh/*.cpp",
  "./geogram/src/lib/geogram/third_party/rply/*.c",
  "./geogram/src/lib/geogram/third_party/zlib/*.c"
]
```

---

## Preprocessor definitions (`compile_definitions`)

Use `compile_definitions` to pass preprocessor switches to the build — most commonly a third-party library's configuration macros. Each entry is `"NAME"` or `"NAME=VALUE"`:

```json
"compile_definitions": [
  "GEOGRAM_USE_BUILTIN_DEPS",
  "GEOGRAM_WITH_HLBFGS"
]
```

(This geogram example selects the vendored libMeshb / rply / zlib over system libraries, and compiles the HLBFGS optimizer in — required by the Newton iterations of CVT remeshing.)

The definitions are emitted as `target_compile_definitions(... PRIVATE ...)` in **two** places, so the whole pipeline sees a consistent configuration:

- the **generator driver** — it includes the bound headers for the reflection walk, which must see the same preprocessor state the bindings will be built with;
- **every compiled binding target** — where they reach the bound headers and the [`user_sources`](#compiling-user-sources-user_sources) alike.

A single string is accepted as a one-element list. The text-only backends (`markdown`, `html`, `json`, `typescript`, `openapi`, `paraview`) compile nothing and ignore it.

---

## Build type & optimization (`build_type`, `optimization`)

Every compiled backend's generated `CMakeLists.txt` can carry a build configuration, so `cmake -S . -B build && cmake --build build` (and `rosetta_gen --build`) produces optimized or debuggable bindings without editing the output:

```json
"build_type": "Release",
"optimization": "-O2"
```

Both are optional and independent:

- **`build_type`** — one of `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` (case-insensitive). Emitted as a *default* inside `if(NOT CMAKE_BUILD_TYPE)`, so `-DCMAKE_BUILD_TYPE=...` at configure time still wins. Omitted ⇒ CMake's usual no-build-type behaviour.
- **`optimization`** — an explicit optimization level: `-O0`, `-O1`, `-O2`, `-O3`, `-Os`, `-Oz`, `-Og` or `-Ofast` (the leading `-` may be omitted). Emitted as `add_compile_options(...)` / `add_link_options(...)`, which land *after* the build type's own per-config flags on the compiler command line — so this `-O` overrides the level the build type implies (e.g. `"build_type": "Release", "optimization": "-O2"` builds `-DNDEBUG` but at `-O2` instead of Release's `-O3`). The link option matters for the wasm targets, where emscripten optimizes at link time too.

The flags apply to the *bindings* (and any [`user_sources`](#compiling-user-sources-user_sources) compiled into them), in every compiled backend — thin and `-expanded` alike. The text-only backends compile nothing and ignore both.

---

## Full reference example

```json
{
  "//1": "Bindings for the geom library, mixing per-target module names,",
  "//2": "out-of-line annotations and a free function.",

  "user_include": "./geom",
  "rosetta_include": "../../include",
  "generator_name": "geom_gen",
  "module_name": "geom",

  "//cpp26": "C++26 / P2996 reflection toolchain used to build the thin backends.",
  "cpp26_root": "$ENV{HOME}/devs/c++/clang-p2996/build",
  "cpp26_cxx":  "$ENV{HOME}/devs/c++/clang-p2996/build/bin/clang++",
  "cpp26_cc":   "$ENV{HOME}/devs/c++/clang-p2996/build/bin/clang",
  "cpp26_lib":  "$ENV{HOME}/devs/c++/clang-p2996/build/lib",

  "//build": "Default build type + explicit -O level for every compiled backend.",
  "build_type": "Release",
  "optimization": "-O2",

  "targets": [
    { "lang": "python-expanded", "name": "geom" },
    { "lang": "nanobind-expanded", "name": "geom" },
    { "lang": "node-expanded", "name": "geom" },
    { "lang": "wasm-expanded", "name": "geom" },
    { "lang": "typescript" },
    { "lang": "markdown" },
    { "lang": "html" }
  ],

  "classes": [
    { "doc": "the top-level model", "name": "Model", "header": "Model.h", "annotations": "Model.ann.json" },
    { "header": "Point.h" },
    { "header": "Surface.h" },
    { "header": "Triangle.h" },
    { "header": "Kind.h" }
  ],

  "functions": [
    { "doc": "Scale and swizzle a point into (x*2, z*3, y*4)",
      "header": "common.h", "name": "transform" }
  ]
}
```

The `cpp26_*` fields point at the **C++26 / P2996 reflection compiler** used to build the thin backends. They are all optional — omit them and rosetta uses these defaults:

| Field | Default |
|---|---|
| `cpp26_root` | `$ENV{HOME}/devs/c++/clang-p2996/build` |
| `cpp26_cxx` | `${cpp26_root}/bin/clang++` |
| `cpp26_cc` | `${cpp26_root}/bin/clang` |
| `cpp26_lib` | `${cpp26_root}/lib` |

If your [Bloomberg `clang-p2996`](https://github.com/bloomberg/clang-p2996) build lives elsewhere, set `cpp26_root` alone — `cpp26_cxx` / `cpp26_cc` / `cpp26_lib` all derive from it. Override the individual ones only when the compiler binaries or the `libc++` / `libc++abi` directory sit outside the usual `bin/` and `lib/` layout. `$ENV{HOME}` is expanded by CMake at configure time, so the path stays portable across machines. These only affect the *thin* backends — the `-expanded` and text targets build with a stock compiler and ignore them.

---

## Common gotchas

- **Paths are relative to the manifest file.** Move it and re-run `rosetta_gen`.
- A class missing from `classes[]` is **invisible** to the bindings.
- A bare-string target reuses `module_name`; if two object-form targets share a `name`, they share a module name — usually intended (one module per language), but watch for collisions across languages that write to the same directory.
- Comments must be valid JSON: use `"//"`-prefixed keys, not `//` line comments.
- The generator host always needs the C++26 / P2996 toolchain; only the `-expanded` and text targets build on a stock compiler afterwards.