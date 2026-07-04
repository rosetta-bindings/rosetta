# `manifest.json` — the rosetta manifest

The **manifest** is the single hand-written file that drives rosetta. You point it at your existing C++ headers, list the languages you want bindings
for, and `rosetta_gen` does the rest. Your class definitions are never modified.

It answers three questions for the framework:

1. **Where** are your headers and where is rosetta's `include/`?
2. **What** classes / free functions should be bound?
3. **Which** language backends (targets) should be emitted?

Everything else — fields, methods, constructors, enums, inheritance — is discovered by C++26 reflection from the headers themselves. You declare *what* to bind, never *how*.

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

```json
{
  "user_include": "../my_lib",
  "rosetta_include": "../../include",
  "generator_name": "my_person_gen",
  "targets": ["python", "node", "rest", "wasm"],
  "classes": [
    { "name": "Person", "header": "person.h" }
  ]
}
```

Build & run:

```bash
# 1. build the framework tool once
cmake -G Ninja -S tools/rosetta_gen -B tools/rosetta_gen/build
cmake --build tools/rosetta_gen/build

# 2. manifest → project generator source
./tools/rosetta_gen/build/rosetta_gen path/to/manifest.json

# 3. build & run the generated <generator_name>
cmake -G Ninja -S path/to/generated -B path/to/generated/build
cmake --build path/to/generated/build
./path/to/generated/build/my_person_gen path/to/output
```

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
| `user_lib` | — | — | Link the generated bindings against a pre-built external library. See [Linking an external library](#linking-an-external-library-user_lib). |
| `user_sources` | — | `[]` | List of user `.cpp` (or `.c`) files compiled directly into every generated binding target. See [Compiling user sources](#compiling-user-sources-user_sources). |
| `compile_definitions` | — | `[]` | Preprocessor definitions (`"NAME"` or `"NAME=VALUE"`) applied to the generator driver and every compiled binding target. See [Preprocessor definitions](#preprocessor-definitions-compile_definitions). |
| `plugins` | — | `[]` | Extra `.cpp` sources to compile into the generator driver (e.g. a custom backend). Paths relative to the manifest. |
| `qt_dir` | — | a built-in path | Qt 6 install prefix used by the `qt` / `qml` (and `-expanded`) backends. e.g. `"$ENV{HOME}/Qt/6.8.3/macos"`. |
| `cpp26_root` | — | `$ENV{HOME}/devs/c++/clang-p2996/build` | Root of the C++26 / P2996 reflection toolchain used by the *thin* backends. Moves `cpp26_cxx` / `cpp26_cc` / `cpp26_lib` together. |
| `cpp26_cxx` | — | `${cpp26_root}/bin/clang++` | C++ compiler (name or path) for the reflection toolchain. |
| `cpp26_cc` | — | `${cpp26_root}/bin/clang` | C compiler (name or path). |
| `cpp26_lib` | — | `${cpp26_root}/lib` | Directory holding the fork's `libc++` / `libc++abi` (`-L` / rpath). |

Keys beginning with `//` (e.g. `"//1"`, `"//note"`) are treated as comments
and ignored — handy since JSON has no comment syntax.

---

## Classes

`classes` is an array of per-class entries. Each binds one C++ type
(`class`, `struct`, or `enum`).

```json
"classes": [
  { "name": "Model", "header": "Model.h", "doc": "the model class" },
  { "header": "Point.h" },
  { "name": "space::Vector3", "header": "Vector3.h",
    "annotations": "Vector3.ann.json" }
]
```

| Field | Required | Default | Meaning |
|---|:---:|---|---|
| `header` | ✅ | — | Filename emitted into `#include "..."`. Resolved against `user_include`. |
| `name` | — | header basename | C++ type name, must be reachable after including `header`. May be namespace-qualified (`space::Vector3`). |
| `annotations` | — | — | Path (relative to the manifest) to an out-of-line annotation JSON side-car, baked into `bindings.h` so the header stays clean. See [OUT_OF_LINE_ANNOTATIONS](OUT_OF_LINE_ANNOTATIONS.md). |
| `doc` | — | — | A description string for the class (used by doc backends). |

Inheritance, multiple constructors, enums, nested user types and
`std::vector` members are discovered automatically — no entry needed per
base class, just list the most-derived type you want bound.

---

## Multiple include directories

When your headers don't all live under a single root, give `user_include`
an **array** of directories instead of a single string:

```json
"user_include": ["./geom", "../shared/include", "/opt/thirdparty/include"]
```

Each entry follows the same resolution rules as the single-string form
(relative to the manifest, or absolute). Every directory is added to the
generated bindings' include path, so a class `header` is resolved against
**all** of them — the first directory that contains the file wins, exactly
like a compiler's `-I` search order. The array must not be empty.

The single-string form is just the one-directory shorthand:

```json
"user_include": "./geom"          // equivalent to ["./geom"]
```

---

## Functions

`functions` binds **free (non-member)** functions without editing your
headers. Each entry:

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

`name` is optional in the object form too (defaults to `module_name`). One
generator emits a **single combined module per target** exposing every class.

The object form also accepts **`link_options`** — extra linker flags applied
to *this target only*, emitted as `target_link_options(... PRIVATE ...)` in
the generated project. Per-target (unlike `compile_definitions`) because link
flags are toolchain-specific — e.g. geogram's `GEO::initialize()` mounts the
host filesystem with NODEFS under Node, which needs emscripten's nodefs
library on the **wasm** link line and would break a native link:

```json
"targets": [
  { "lang": "python-expanded" },
  { "lang": "wasm-expanded", "link_options": ["-lnodefs.js"] }
]
```

### Available `lang` values

Thin (reflection re-runs at the target's compile time — needs the C++26
toolchain to build) and **expanded** (reflection runs once on the host;
the generated code builds with a stock compiler).

| `lang` | Output | Expanded variant |
|---|---|---|
| `python` | pybind11 extension module | `python-expanded` |
| `nanobind` | nanobind extension module | `nanobind-expanded` |
| `node` | N-API native addon | `node-expanded` |
| `wasm` | Emscripten / embind module | `wasm-expanded` |
| `qt` | Qt Widgets property/method inspector | `qt-expanded` |
| `qml` | QML / QtQuick inspector | `qml-expanded` |
| `csharp` | C-ABI shared lib + P/Invoke wrappers | `csharp-expanded` |
| `java` | C-ABI + handle-backed FFM wrappers | `java-expanded` |
| `julia` | CxxWrap.jl / jlcxx shared module | — |
| `rest` | cpp-httplib JSON server + browser client | — |
| `openapi` | OpenAPI 3.1 spec | text output |
| `json` | nlohmann (de)serialization | text output |
| `typescript` | ambient `.d.ts` declarations | text output |
| `markdown` | API reference document | text output |
| `html` | styled API reference page | text output |
| `paraview` | ParaView Server Manager plugin XML | text output |

The text-only outputs (`markdown`, `html`, `json`, `typescript`, `openapi`,
`paraview`) don't compile anything, so the C++26-vs-stock distinction
doesn't apply — they're produced directly.

> **Why expanded?** If your *target* compiler doesn't support reflection,
> use the `-expanded` variants: generate once on a C++26 / P2996 host, then
> ship and build the generated sources anywhere with a stock toolchain
> (plain Clang / GCC / MSVC, stock emsdk, stock Qt 6). The generator host
> still needs C++26; the target does not. Pairs naturally with out-of-line
> annotations so the bound headers stay stock C++ too. See
> [`examples/geom-expanded`](../examples/geom-expanded).

---

## Linking an external library (`user_lib`)

Use `user_lib` when your bound headers only **declare** the API and the
definitions live in a separately-compiled library (`.so` / `.dylib` / `.a`).
rosetta links the generated bindings against it and sets up rpath.

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

`wasm` targets are **always** static — a native `.dylib` / `.so` cannot
enter a wasm module. The native `python` / `node` targets honor `link`. See
[`examples/dynamic-lib`](../examples/dynamic-lib).

---

## Compiling user sources (`user_sources`)

Use `user_sources` when your bound headers only **declare** the API and the
definitions live in `.cpp` files you want **compiled straight into the
binding** — rather than linked from a pre-built [`user_lib`](#linking-an-external-library-user_lib).

```json
"user_sources": [
  "../src/widget.cpp",
  "../src/shape.cpp"
]
```

It is always a **list of paths**, each relative to the manifest (or
absolute). Every compiled backend adds them to its binding target via
`target_sources(...)`, so they build with the same include path and flags as
the generated binding. A single string is accepted as a one-element list.

Each entry may be a **shell glob**, expanded at generation time:

```json
"user_sources": ["./extern/pmp/src/pmp/algorithms/*.cpp"]
```

`*`, `?` and `[...]` are supported within a path component (POSIX glob — not
recursive `**`). Matches are sorted for reproducible output, and the final
list is de-duplicated, so mixing a literal path with a glob that also covers
it is safe. A pattern that matches nothing emits a warning and is skipped.
Because globs expand when `rosetta_gen` runs, **re-run it after adding or
removing matching files**.

`user_sources` and `user_lib` are independent — use either, or both. The
text-only backends (`markdown`, `html`, `json`, `typescript`, `openapi`,
`paraview`) compile nothing and ignore it.

Entries may also be **C sources** (`.c`) — e.g. a third-party library's
vendored dependencies (zlib, rply, libMeshb, OpenNL…). When any `.c` file is
listed, the generated CMakeLists calls `enable_language(C)` automatically so
they compile alongside the C++ binding:

```json
"user_sources": [
  "./geogram/src/lib/geogram/mesh/*.cpp",
  "./geogram/src/lib/geogram/third_party/rply/*.c",
  "./geogram/src/lib/geogram/third_party/zlib/*.c"
]
```

---

## Preprocessor definitions (`compile_definitions`)

Use `compile_definitions` to pass preprocessor switches to the build — most
commonly a third-party library's configuration macros. Each entry is `"NAME"`
or `"NAME=VALUE"`:

```json
"compile_definitions": [
  "GEOGRAM_USE_BUILTIN_DEPS",
  "GEOGRAM_WITH_HLBFGS"
]
```

(This geogram example selects the vendored libMeshb / rply / zlib over system
libraries, and compiles the HLBFGS optimizer in — required by the Newton
iterations of CVT remeshing.)

The definitions are emitted as `target_compile_definitions(... PRIVATE ...)`
in **two** places, so the whole pipeline sees a consistent configuration:

- the **generator driver** — it includes the bound headers for the reflection
  walk, which must see the same preprocessor state the bindings will be built
  with;
- **every compiled binding target** — where they reach the bound headers and
  the [`user_sources`](#compiling-user-sources-user_sources) alike.

A single string is accepted as a one-element list. The text-only backends
(`markdown`, `html`, `json`, `typescript`, `openapi`, `paraview`) compile
nothing and ignore it.

---

## Full reference example

```json
{
  "//": "Bindings for the geom library, mixing per-target module names,",
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

  "targets": [
    { "lang": "python", "name": "geom" },
    { "lang": "nanobind", "name": "geom" },
    { "lang": "node", "name": "geom" },
    { "lang": "wasm-expanded", "name": "geom" },
    { "lang": "typescript" },
    { "lang": "markdown" },
    { "lang": "html" }
  ],

  "classes": [
    { "doc": "the top-level model", "name": "Model", "header": "Model.h",
      "annotations": "Model.ann.json" },
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

The `cpp26_*` fields point at the **C++26 / P2996 reflection compiler** used
to build the thin backends. They are all optional — omit them and rosetta
uses these defaults:

| Field | Default |
|---|---|
| `cpp26_root` | `$ENV{HOME}/devs/c++/clang-p2996/build` |
| `cpp26_cxx` | `${cpp26_root}/bin/clang++` |
| `cpp26_cc` | `${cpp26_root}/bin/clang` |
| `cpp26_lib` | `${cpp26_root}/lib` |

If your [Bloomberg `clang-p2996`](https://github.com/bloomberg/clang-p2996)
build lives elsewhere, set `cpp26_root` alone — `cpp26_cxx` / `cpp26_cc` /
`cpp26_lib` all derive from it. Override the individual ones only when the
compiler binaries or the `libc++` / `libc++abi` directory sit outside the
usual `bin/` and `lib/` layout. `$ENV{HOME}` is expanded by CMake at
configure time, so the path stays portable across machines. These only
affect the *thin* backends — the `-expanded` and text targets build with a
stock compiler and ignore them.

---

## Common gotchas

- **Paths are relative to the manifest file.** Move it and re-run
  `rosetta_gen`.
- A class missing from `classes[]` is **invisible** to the bindings.
- A bare-string target reuses `module_name`; if two object-form targets
  share a `name`, they share a module name — usually intended (one module
  per language), but watch for collisions across languages that write to
  the same directory.
- Comments must be valid JSON: use `"//"`-prefixed keys, not `//` line
  comments.
- The generator host always needs the C++26 / P2996 toolchain; only the
  `-expanded` and text targets build on a stock compiler afterwards.
