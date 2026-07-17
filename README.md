<p align="center">
  <img src="media/logo-rosetta.png" alt="rosetta" width="400">
</p>

# Rosetta

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License: MIT"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-26-blue.svg?logo=cplusplus" alt="C++26">
  <img src="https://img.shields.io/badge/status-prototype-yellow.svg" alt="Status: prototype">
  <a href="https://xaliphostes.github.io/rosetta/#1"><img src="https://img.shields.io/badge/slides-rosetta-blue?logo=marp" alt="Slides"></a>
  <a href="https://github.com/xaliphostes/rosetta2/stargazers"><img src="https://img.shields.io/github/stars/xaliphostes/rosetta2?style=social" alt="GitHub stars"></a>
</p>

<p align="center">
  <a href="https://github.com/bloomberg/clang-p2996"><img src="https://img.shields.io/badge/clang--p2996-tested-brightgreen.svg?logo=llvm" alt="clang-p2996: tested"></a>
  <img src="https://img.shields.io/badge/EDG-experimental-yellow.svg" alt="EDG: experimental">
  <img src="https://img.shields.io/badge/NVC%2B%2B-planned-lightgrey.svg?logo=nvidia" alt="NVC++: planned">
  <img src="https://img.shields.io/badge/GCC-in%20progress-lightgrey.svg?logo=gnu" alt="GCC: in progress">
  <img src="https://img.shields.io/badge/Clang%20%7C%20MSVC-tracking-lightgrey.svg" alt="Clang | MSVC: tracking">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/macOS-tested-brightgreen.svg?logo=apple" alt="macOS: tested">
  <img src="https://img.shields.io/badge/Linux-untested-lightgrey.svg?logo=linux&logoColor=white" alt="Linux: untested">
  <img src="https://img.shields.io/badge/Windows-untested-lightgrey.svg" alt="Windows: untested">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/bindings-Python%20%7C%20Node%20%7C%20Wasm%20%7C%20TypeScript%20%7C%20Lua%20%7C%20Julia%20%7C%20Csharp%20%7C%20Java-green.svg" alt="Bindings">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/bindings-Qt%20%7C%20QML%20%7C%20ImGUI%20%7C%20ParaView%20%7C%20Json%20%7C%20Html%20%7C%20REST%20%7C%20OpenAPI%20%7C%20Markdown-green.svg" alt="Bindings">
</p>

A C++26 reflection playground with **27 generator backends** — Python (pybind11 / nanobind), Node, WebAssembly, Qt, QML, Dear ImGui, REST, Julia, Lua, OpenAPI, JSON, TypeScript, C#, Java, Markdown, HTML, ParaView... bindings for **your existing classes — without modifying them**. Point rosetta at a header via a small [manifest.json](./docs/MANIFEST.md), run one tool, get per-language binding projects out.

> **Your target compiler doesn't support reflection?** Generate the expanded binding once on a Linux or macOS host with a C++26 / P2996 compiler — e.g. the [Bloomberg `clang-p2996`](https://github.com/bloomberg/clang-p2996) fork — then ship and build the generated sources anywhere with a stock toolchain (plain Clang / GCC / MSVC, or a stock emsdk for WebAssembly). No reflection is needed on the target (see the **expanded** backends below).

Annotations (`doc`, `range`, `readonly`, …) are an *opt-in* enrichment, not a requirement: add them where you want docstrings, validation, or UI hints; leave the rest of the class alone. Reflection does the work either way.

## Features

<details>
<summary>Fields, methods, inheritance, constructors, enums, free functions, <code>std::vector</code> — discovered by reflection; opt-in annotations enrich, inline or out of line <i>(expand)</i></summary>

Everything below is discovered by **reflection** from your unmodified headers — you declare *what* to bind in [manifest.json](./docs/MANIFEST.md), never *how*.

**What rosetta can bind**

- **Public fields** — exposed as read/write properties, with per-backend getters/setters.
- **Public methods** — both instance and `static` members.
- **Inheritance** — public base-class fields and methods are flattened into the derived binding; a derived declaration shadows the base one (most-derived wins) and a virtual diamond collapses to a single member. Virtual / overriding methods are flagged (`virtual_spec`) so backends can tell them apart from plain ones.
- **Multiple constructors** — default *and* parameterized; each overload is bound.
- **Enums** — `enum` / `enum class`, with enumerators surfaced as named constants.
- **Free (non-member) functions** — declared in the [manifest](./docs/MANIFEST.md), no edit to your headers ([details](docs/FREE_FUNCTIONS.md)).
- **Nested user types & `std::vector`** — `Surface` returning `Point`/`Triangle`, vector members, etc. are marshalled across the language boundary.
- Members a backend can't marshal (e.g. `std::function` params) are **skipped**, not fatal.

**Opt-in annotations** (enrich without intruding — see the [annotation reference](docs/ANNOTATIONS.md))

- `doc{"..."}` — docstrings / generated reference text.
- `readonly` — read-only property (write is rejected per backend).
- `range{lo, hi}` — value-range validation on assignment.
- `combobox{{...}}` — enumerated choices (UI hint).

Don't want to touch the header at all? Provide the same annotations **out of line**, from a JSON string attached to the type elsewhere — even in another file:

```cpp
struct MyClass {
    int value = 0;
};

// Add annotation later on in another file
template <>
constexpr std::string_view rosetta::ann_json_source<MyClass> = 
  R"({ "value": { "range": [1, 9] } })";
```

In a manifest-driven build you don't write that by hand: add an `"annotations": "Type.ann.json"` field to the class in `manifest.json` and rosetta bakes the external file in for you. Either way the metadata is merged with any inline annotations and reaches every backend (Python, Node, REST, OpenAPI, …) — see [out-of-line annotations](docs/OUT_OF_LINE_ANNOTATIONS.md).

</details>

## Backends (one combined module per target, from a single generator)

| # | Target | C++26 | C++20 |
|---|---|:---:|:---:|
| 1 | **Python** — pybind11 extension module | ✅ | — |
| 1b | **Python (expanded)** — fully-expanded pybind11 | ✅ | ✅ |
| 2 | **Python (nanobind)** — leaner/faster pybind11 successor | ✅ | — |
| 2b | **Python (nanobind, expanded)** — fully-expanded nanobind | ✅ | ✅ |
| 3 | **Node** — N-API native addon | ✅ | — |
| 3b | **Node (expanded)** — fully-expanded N-API | ✅ | ✅ |
| 4 | **Julia** — CxxWrap.jl / jlcxx shared module | ⚠️ | — |
| 4b | **Julia (expanded)** — fully-expanded jlcxx, `std::vector` included | ✅ | ✅ |
| 5 | **WebAssembly** — Emscripten/embind module | ⚠️ | — |
| 5b | **WebAssembly (expanded)** — fully-expanded embind | ✅ | ✅ |
| 6 | **Qt Widgets** — live property/method inspector (`QtVisitor`) | ✅ | — |
| 6b | **Qt Widgets (expanded)** — generated inspector via `qt_widgets_runtime.h` | ✅ | ✅ |
| 7 | **Dear ImGui (expanded)** — immediate-mode inspector app (GLFW + OpenGL3, auto-fetched) | ✅ | ✅ |
| 8 | **QML** — QtQuick inspector via a generic `ReflectedObject` (`QmlVisitor`) | ✅ | — |
| 8b | **QML (expanded)** — fills the generic `ReflectedObject` explicitly | ✅ | ✅ |
| 9 | **REST** — cpp-httplib JSON server + generated browser client| ✅ | — |
| 10 | **OpenAPI** — OpenAPI 3.1 spec describing the REST surface | ✅ | ✅ |
| 11 | **JSON** — reflection-based nlohmann (de)serialization (`json_visitor.h`) | ✅ | — |
| 12 | **TypeScript** — ambient `.d.ts` type declarations | ✅ | ✅ |
| 13 | **Markdown** — API reference document | ✅ | ✅ |
| 14 | **HTML** — self-contained, styled API reference page | ✅ | ✅ |
| 15 | **ParaView** — Server Manager XML for a plugin | ✅ | ✅ |
| 16 | **C#** — native C-ABI shared library + handle-backed P/Invoke wrappers | ✅ | — |
| 16b | **C# (expanded)** — same wrapper (stock C++20) | ✅ | ✅ |
| 17 | **Java** — native C-ABI + handle-backed FFM wrappers | ✅ | — |
| 17b | **Java (expanded)** — same wrapper (stock C++20) | ✅ | ✅ |
| 18 | **Lua (expanded)** — fully-expanded sol2 module, `require`-able (stock C++17) | ✅ | ✅ |

> **C++26** = builds against the reflection toolchain (⚠️ = with caveats — see notes below). **C++20** = the generated target also builds on a stock, pre-reflection toolchain (no reflection needed on the target); text-only outputs qualify trivially. The generator itself always needs C++26.
>
> Notes: **Julia** (thin) builds & runs but skips `std::vector` (fork libc++ gap) — **julia-expanded** builds against the stock libc++, so vectors (including vectors of bound classes) are fully bound; **WebAssembly** (thin) needs a reflection-aware emsdk, while **wasm-expanded** builds with a stock emsdk (`std::vector` via `register_vector`); the **expanded** Qt/QML targets need Qt 6 but no moc on the generated code; **lua-expanded** needs Lua 5.1–5.4 or LuaJIT (sol2 does not support Lua 5.5 yet) — sol2 itself is fetched automatically at configure time.

> New backends register without touching the generator, thanks to the visitor pattern — see [EXTENDING_BACKEND](docs/EXTENDING_BACKEND.md).

**Expanded (reflection-free) targets.** The default `python` / `nanobind` / `node` / `wasm` / `qt` / `qml` / `csharp` / `java` backends emit a *thin* binding that re-runs the reflection walk at the target's compile time, so building the binding also needs the C++26 toolchain. The `python-expanded`, `nanobind-expanded`, `node-expanded`, `wasm-expanded`, `qt-expanded`, `qml-expanded`, `csharp-expanded`, `java-expanded`, `julia-expanded` and `lua-expanded` targets instead **fully expand** every field, method, constructor and enumerator into explicit pybind11 / nanobind / N-API / embind / Qt / sol2 / jlcxx / member-pointer calls. Reflection runs once, on the generation host; the generated binding is ordinary C++ that builds with a stock compiler — a plain C++17/20 compiler, a stock emsdk, or stock Qt 6 (the host still needs C++26 to *run the generator*, the target does not). This pairs naturally with [out-of-line annotations](docs/OUT_OF_LINE_ANNOTATIONS.md) so the bound headers stay stock C++ too — see [`examples/geom-expanded`](examples/geom-expanded).

## Mini-MOC — Qt signals / slots / properties, without moc

<details>
<summary>A header-only, <b>moc-less</b> take on signals/slots/properties, built on reflection + annotations — annotate members, <code>connect&lt;"sig","slot"&gt;</code>, done <i>(expand)</i></summary>

Beyond binding generation, rosetta ships [`mini_moc.h`](include/rosetta/mini_moc.h): a header-only, **moc-less** reimagining of Qt's signals/slots/properties built directly on C++26 reflection (P2996) + annotations (P3394). No code generator, no separate compile step — just annotate members and connect them.

You mark members with annotations and reach them through three free functions:

```cpp
#include <rosetta/mini_moc.h>
namespace moc = rosetta::moc;

class Person {
public:
    moc::Signal<std::string const &> nameChanged;   // a Signal<...> member IS a signal
    moc::Signal<int>                 ageChanged;

    [[= moc::property{"name", "nameChanged"}]] std::string m_name;
    [[= moc::property{"age",  "ageChanged"}]]  int         m_age = 0;
    [[= moc::property{"id"}]]                  int         m_id  = 0;   // no NOTIFY
};

struct Logger {
    [[= moc::slot]] void onAge(int v)               { total += v; }
    [[= moc::slot]] void onName(std::string const&) { /* ... */ }
    int total = 0;
};

Person p; Logger l;
moc::connect<"ageChanged", "onAge">(p, l);  // compile-time checked
moc::set<"age">(p, 30);                      // equality-gated; fires NOTIFY -> Logger::onAge
moc::get<"age">(p);                          // -> 30
```

- **Signals need no annotation** — any `Signal<...>` data member is a signal, recognized by its type. `slot` and `property{"name", "notifySig"}` annotations mark the members that *aren't* self-identifying; reflection discovers them.
- **`connect<"sig","slot">(sender, receiver)`** — compile-time checked: a wrong name is a `static_assert`, mismatched argument types are a template error.
- **`get<"prop">` / `set<"prop">`** — property access from outside the class (token injection, P3294, isn't in clang-p2996 yet, so accessors aren't emitted into the class body). `set<>` is equality-gated and fires the property's `NOTIFY` signal only on an actual change.
- **`Signal<Args...>`** — the only machinery type you spell out; supports `connect` / `disconnect` / `disconnect_all`, re-entrant self-disconnect, and a `ScopedConnection` RAII handle for scope-bound connections.

See the [`examples/moc`](examples/moc) demo and the test suite in [`tests/moc.cpp`](tests/moc.cpp).

</details>

## Status

<details>
<summary>Prototype — tracks P2996 / P3394 / P3294; <b>clang-p2996 today</b>, EDG the likely next target <i>(expand)</i></summary>

Prototype. Tracks the in-flight C++26 reflection papers:

- **P2996** — reflection (`^^T`, `[: r :]` splice, `std::meta::*`)
- **P3394** — annotation attributes (`[[= rosetta::doc{"..."}]]`)
- **P3294** — token injection (not yet used; see notes in `mini_moc.h`)

Builds with the Bloomberg [clang-p2996 fork](https://github.com/bloomberg/clang-p2996) — the reference implementation rosetta is developed and tested against.

No mainline compiler ships these proposals yet, but other front-ends are implementing P2996 and should become viable targets as their support matures (and as rosetta's compiler-specific flags are abstracted):

- **clang-p2996** (Bloomberg fork) — ✅ supported today; what rosetta is built and tested with.
- **EDG** — front-end has an experimental P2996 implementation; the most likely next target.
- **NVC++ / NVHPC** — built on the EDG front-end, so it could inherit reflection as EDG's support lands in releases.
- **GCC** — reflection is under active development on experimental branches; not yet usable for rosetta.
- **Mainline Clang / MSVC** — tracking P2996 but no usable implementation yet.

Annotations (P3394) and token injection (P3294) are newer and currently exist only in the clang-p2996 fork, so full-feature builds remain fork-only for now.

</details>

## Requirements

<details>
<summary>A clang-p2996 build, CMake 3.28+, and the fork's reflection flags <i>(expand)</i></summary>

- A clang-p2996 build at `$HOME/devs/c++/clang-p2996/build` (or override `CLANG_P2996_ROOT` when invoking cmake).
- CMake 3.28+, Ninja or Make.
- C++26 mode with the fork's flags: `-freflection -freflection-latest -fexperimental-library`. Annotation-using code also needs `-fannotation-attributes`.

</details>

## Build the test suite

<details>
<summary><code>cd tests && cmake -B build && cmake --build build</code> — each test is a self-contained binary <i>(expand)</i></summary>

```bash
cd tests
cmake -B build
cmake --build build
./build/hello
./build/moc
./build/docgen
# ...
```

Each test is self-contained; pick by name (see `tests/CMakeLists.txt`).

</details>

## A taste — bind your existing library

<details>
<summary>An untouched <code>person.h</code> + a small <code>manifest.json</code> → Python and Node modules in four commands <i>(expand)</i></summary>

You have this header. Don't change it:

```cpp
// my_lib/person.h
#include <string>

struct Person {
    std::string name;
    int         age = 0;
    std::string id;
    std::string greet(const std::string &salutation) const;
};
```

Write a small [manifest.json](./docs/MANIFEST.md) next to it. Each `targets` entry names the module/library produced for that backend; list every class you want bound:

```json
{
  "user_include": "../my_lib",
  "rosetta_include": "/path/to/rosetta/include",
  "targets": [
    { "lang": "python", "name": "person_py" },
    { "lang": "node",   "name": "person_js" }
  ],
  "classes": [
    { "name": "Person", "header": "person.h" }
  ]
}
```

Once the scaffolder is built (first block below), one command runs the rest of the pipeline — generation, generator build, and a compile of every backend (skipping any whose toolchain is missing):

```bash
/path/to/rosetta/bin/rosetta_gen --build manifest.json    # → bindings/, compiled; --help lists the options
```

Or step by step — generate, build, and run the project-specific tool it emits:

```bash
# (one-time) build the framework scaffolder → <repo>/bin/rosetta_gen
cmake -S tools/rosetta_gen -B tools/rosetta_gen/build
cmake --build tools/rosetta_gen/build

# from the folder holding manifest.json:
#   write the generator project (bindings.h + <generator_name>.cpp + CMakeLists.txt)
#   into a folder you name — here `gen/`
/path/to/rosetta/bin/rosetta_gen manifest.json gen

# build it — the `generator` binary is dropped into the current folder,
# not the build tree
cmake -S gen -B gen/build && cmake --build gen/build

# run it → one combined module per backend under bindings/
./generator bindings
```

Result: `bindings/{python,node}/` — each a self-contained CMake project exposing **all** your classes in a single module. `cd bindings/python && cmake -B build && cmake --build build`, then `import person_py`.

> `generator_name` and `module_name` are optional manifest fields: `generator_name` (the generated `.cpp` / usage name) defaults to the manifest's folder name, and a bare-string target like `"node"` falls back to `module_name` for its module name.

The full walkthrough is in [`docs/QUICKSTART.md`](./docs/QUICKSTART.md); every manifest field is documented in [`docs/MANIFEST.md`](./docs/MANIFEST.md); the `binding_info<T>` trait and the layered tooling model are in [`docs/GENERATE.md`](./docs/GENERATE.md). The worked examples live in `examples/manifest/` and `examples/geom-lib/`.

</details>

## Extending a generated binding in C++

<details>
<summary>Add a hand-written registration block <b>beside</b> the generated source — regeneration never clobbers your extensions <i>(expand)</i></summary>

Everything under `bindings/` is regenerated output — never edit it. When the stock binding misses something you need (a helper the walker skips, such as an overloaded free function; a custom view over a type that isn't bound; a typed-array export for a renderer), add it in a **separate hand-written C++ file** and compile it *alongside* the generated source, from a small build of your own that lives outside `bindings/`. The binding frameworks accept several registration blocks per module, so your file simply contributes a second one — nothing generated is touched, and regenerating the bindings never clobbers your extensions.

A complete worked example is [`pmp-rosetta/wasm-viz`](https://github.com/rosetta-bindings/pmp-rosetta/tree/main/wasm-viz): a stand-alone WebAssembly build for a three.js viewer that compiles the generated `bindings/wasm-expanded/auto_emscripten.cpp` verbatim plus one hand-written `viz_helpers.cpp`, which adds what the auto-generated binding does not expose — flat vertex/index buffers as JS typed arrays, and a wrapper for an overloaded function the generator skips — in its own `EMSCRIPTEN_BINDINGS` block:

```cpp
// viz_helpers.cpp — compiled next to the generated auto_emscripten.cpp
emscripten::val mesh_positions(const pmp::SurfaceMesh &mesh);  // Float32Array
emscripten::val mesh_triangles(const pmp::SurfaceMesh &mesh);  // Uint32Array
void triangulate_mesh(pmp::SurfaceMesh &m) { pmp::triangulate(m); } // overload → skipped by the generator

EMSCRIPTEN_BINDINGS(pmp_viz) { // second block; the generated one keeps its own
    emscripten::function("mesh_positions", &mesh_positions);
    emscripten::function("mesh_triangles", &mesh_triangles);
    emscripten::function("triangulate_mesh", &triangulate_mesh);
}
```

Embind is the friendliest here because it accepts any number of `EMSCRIPTEN_BINDINGS` blocks in one module. Backends with a single module entry point (pybind11's `PYBIND11_MODULE`, N-API's `NODE_API_MODULE`) can't add a second block to the *same* module — there, ship your extras as a small companion module built the same way (it sees the same user headers and links the same library), and import/require both.

</details>

## Examples

<details>
<summary><b>17 worked examples</b> — manifest-driven, expanded targets, trampolines, mini-moc, the three UI inspectors, hand-written references <i>(expand)</i></summary>

| Path                       | What it shows                                       |
|----------------------------|-----------------------------------------------------|
| `examples/manifest`        | Manifest-driven generation for `Person` (no class modification) |
| `examples/annotate-manifest`| Out-of-line annotations from an external JSON file, wired by the manifest's `annotations` field ([details](docs/OUT_OF_LINE_ANNOTATIONS.md)) |
| `examples/geom-lib`        | Manifest-driven bindings for a small geometry library (nested types, vectors) |
| `examples/geom-expanded`   | Reflection-free `python-expanded` / `nanobind-expanded` / `node-expanded` / `wasm-expanded` / `qt-expanded` / `qml-expanded` / `csharp-expanded` / `java-expanded` / `lua-expanded` / `julia-expanded` bindings (stock compiler, stock emsdk, stock Qt, any Lua 5.1–5.4, CxxWrap.jl) with out-of-line annotations |
| `examples/trampoline`      | Overriding C++ virtuals from Python — generated pybind11 trampolines from `virtual_spec` |
| `examples/trampoline-node` | Overriding C++ virtuals from JavaScript — generated N-API trampolines from `virtual_spec` |
| `examples/moc`             | Qt-flavoured meta-object demo on `mini_moc.h` (properties + signals) |
| `examples/docgen`          | Reflection-driven Markdown / HTML reference generator |
| `examples/paraview`        | ParaView plugin property-panel XML from an annotated `vtkThreshold` spec (every backend feature) |
| `examples/qt`              | Building a Qt widget form from a reflected struct   |
| `examples/qml`             | Exposing a reflected C++ object to QML              |
| `examples/imgui`           | Dear ImGui inspector for `Algo` with out-of-line annotations (`Algo.ann.json`: `doc` / `range` / `combobox`) — builds with a stock C++20 compiler, deps auto-fetched |
| `examples/bindings/python` | Hand-written pybind11 backend (reference)           |
| `examples/bindings/node`   | Hand-written N-API backend (reference)              |
| `examples/bindings/julia`  | Hand-written CxxWrap/jlcxx backend (reference, requires CxxWrap.jl) |
| `examples/bindings/rest`   | Hand-written HTTP/REST backend (reference)          |
| `examples/bindings/web`    | Hand-written WebAssembly backend (requires reflection-aware emsdk) |

</details>

## Rosetta in the wild

<details>
<summary>Real libraries bound with rosetta — <b>PMP, geogram, Arch, Cassini</b> — each from a single <code>manifest.json</code>, no hand-written wrappers <i>(expand)</i></summary>

| Project | Bound library | Targets |
|---|---|---|
| [pmp-rosetta](https://github.com/rosetta-bindings/pmp-rosetta) | [PMP](https://www.pmp-library.org) — the Polygon Mesh Processing library (remeshing, smoothing, subdivision, decimation) | Python, Node.js, WebAssembly, TypeScript |
| [geogram-rosetta](https://github.com/rosetta-bindings/geogram-rosetta) | [geogram](https://github.com/BrunoLevy/geogram) — Bruno Lévy's geometry-processing library (reconstruction, remeshing, parameterization, booleans/CSG) | Python, Node.js, WebAssembly, TypeScript, Lua |
| [arch-rosetta](https://github.com/rosetta-bindings/arch-rosetta) *(private)* | Arch — a 3-D boundary-element (BEM) geomechanics code | Python, Node.js, WebAssembly, TypeScript |
| [cassini-rosetta](https://github.com/rosetta-bindings/cassini-rosetta) *(private)* | Cassini — FEM geomechanical restoration, bound through a single high-level C++ facade | Python, Node.js, WebAssembly, TypeScript |
| [triax-rosetta](https://github.com/rosetta-bindings/triax-rosetta) | Triax — a discrete-element (DEM) triaxial compression simulator for granular packings | Python, Node.js, WebAssembly |

</details>

## Design notes

<details>
<summary>Quick start, the manifest / generate / annotation references, and the todo list <i>(expand)</i></summary>

- [Quick start](docs/QUICKSTART.md) — five-step guide to generating bindings for an existing library
- [Extending](docs/EXTENDING_BACKEND.md) — how to extend the rosetta backend
- [Manifest](docs/MANIFEST.md) — complete reference for `manifest.json`: every field, target and option
- [Annotations](docs/ANNOTATIONS.md) — the full annotation reference (doc / range / readonly / combobox / label / button / widget hints, inline & out-of-line)
<br><br>
- [Generate](docs/GENERATE.md) — full reference for `rosetta::generate`, the manifest schema, and the tool layering
- [Free functions](docs/FREE_FUNCTIONS.md) — sketch for reflecting namespace-scope functions
- [Other annotations](docs/OTHER_ANNOTATIONS.md) — proposed annotation kinds beyond the current set
- [Out-of-line annotations](docs/OUT_OF_LINE_ANNOTATIONS.md) — keep headers clean: a JSON side-car of annotations baked in at generation time, merged at compile time
- [Todo list](docs/TODO.md) — what the walker and visitor surface still miss (static data members, parameter metadata, nested types, ...)

</details>

## License

[MIT](./LICENSE)

## Author
[xaliphostes](https://github.com/xaliphostes)