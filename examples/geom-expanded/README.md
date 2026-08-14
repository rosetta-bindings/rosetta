# geom-expanded — reflection-free bindings

A variant of [`../geom-lib`](../geom-lib) whose generated bindings **build with a
stock toolchain** — no clang-p2996, no reflection, (almost) no rosetta headers on
the machine that compiles the binding. It ships eleven such targets:

- **`python`** → a pybind11 module that builds with a stock **C++17** compiler.
- **`nanobind`** → a [nanobind](https://github.com/wjakob/nanobind) module
  (leaner/faster pybind11 successor) — stock **C++17**, smallest binary of the set.
- **`node`** → an N-API addon that builds with a stock **C++20** compiler
  (uses the header-only, reflection-free `runtime/node.h`).
- **`wasm`** → an emscripten/embind module that builds with a **stock emsdk**
  (no reflection-aware fork — the limitation noted for the plain `wasm` target).
- **`qt`** → a Qt Widgets inspector window (stock **C++17** + Qt 6) via the
  header-only, reflection-free `runtime/qt_widgets.h`; no moc on the generated code.
- **`qml`** → a QtQuick inspector (stock **C++17** + Qt 6) that fills the generic
  `ReflectedObject` bridge; moc runs only on that bridge, never per type.
- **`csharp`** → a native shared library (stock **C++20**) exposing a flat
  C ABI, plus idiomatic handle-backed C# wrappers that reach it through P/Invoke
  (values marshalled as JSON via `System.Text.Json`); ships a `.csproj`. The native
  shim registers every field/method/constructor by *member pointer*, so the runtime
  deduces the marshalled types — no reflection. Out-of-line `range`/`readonly`/`doc`
  flow straight in (e.g. `Triangle.a` rejects an out-of-range value at run time).
- **`java`** → the same C-ABI shim (stock **C++20**) plus handle-backed
  Java wrappers reaching it through the FFM API (`java.lang.foreign`).
- **`lua`** → a [sol2](https://github.com/ThePhD/sol2) module (stock **C++17** +
  Lua 5.1–5.4 / LuaJIT; sol2 fetched automatically) built as a plain
  `require`-able C module (`luaopen_luageom` in `luageom.so`). Vector parameters
  accept plain Lua tables (a second `sol::nested` overload is generated beside
  the exact one), a Lua function converts natively into a `std::function`
  callback parameter, and the out-of-line `range` on `Triangle.a/b/c` validates
  at run time.
- **`julia`** → a CxxWrap / jlcxx module (stock **C++20** + Julia with
  the CxxWrap package) loaded via a generated `jlgeom.jl` wrapper. Because it
  builds against the stock libc++ — not the fork's — `<jlcxx/stl.hpp>` compiles
  and **`std::vector` crosses the boundary** (members, parameters, returns,
  including vectors of bound classes), which the thin `julia` target must skip.
  A plain Julia `Vector` works wherever C++ wants a vector (a zero-copy
  `ArrayRef` overload is generated beside the exact one); fields follow Julia's
  convention (`x(p)` / `x!(p, v)`), and the `range` annotation validates in the
  generated setter.

- **`imgui`** → a Dear ImGui inspector app (stock **C++20**; ImGui and GLFW are
  fetched automatically at configure time) — the immediate-mode counterpart of
  `qt`. One tab per class; ranged fields become clamping sliders
  (`Triangle.a/b/c`), enums become combos, docs become "(?)" tooltips, and
  scalar methods get a call button. `ROSETTA_IMGUI_FRAMES=N` auto-exits after N
  frames (smoke tests).

Two things make the stock-toolchain targets possible:

1. **Out-of-line annotations.** The headers in [`geom/`](geom) are plain C++:
   no `[[ = rosetta::doc{...} ]]`, no rosetta include. Every doc/range lives in a
   `*.ann.json` side-car ([`Point.ann.json`](Point.ann.json),
   [`Triangle.ann.json`](Triangle.ann.json), [`Model.ann.json`](Model.ann.json)),
   wired in by the manifest's `"annotations"` field. So the bound headers
   themselves are stock C++ and parse under any compiler.

2. **Expansion.** Rather than emit a binding that re-runs the reflection walk at
   the target's compile time, every backend fully expands each field, method,
   constructor and enumerator into explicit pybind11 / nanobind / N-API / embind
   calls. The generated TU includes only the binding-framework headers plus the
   (stock) user headers. (Until 2026-08 this was the distinguishing half of a
   `*-expanded` target; the thin variants are gone and every target works this
   way, so the suffix no longer appears in a manifest.)

The docstrings and the `range` validation on `Triangle::a/b/c` are baked into the
generated source as literal C++ — even though the headers carry none — because
reflection runs once on the **generation host**, not on the target. Members a
backend can't marshal (e.g. `Surface::transform`, which takes a `std::function`)
are skipped where unsupported; `std::vector` crosses the boundary in every target
(embind via emitted `register_vector<T>()`).

## Reproduce

### 1. scaffold the driver from the manifest (host: any C++ — rosetta_gen is plain)
```sh
../../bin/rosetta_gen manifest.json gen
```

### 2. build & run the driver (host: needs clang-p2996 — reflection)
```sh
cmake -S gen -B gen/build && cmake --build gen/build -j
./generator bindings
```

### 3a. Python / pybind11 — stock C++17
```sh
cmake -S bindings/python -B bindings/python/build
cmake --build bindings/python/build -j
```

### 3b. Python / nanobind — stock C++17 (needs the pip `nanobind` package)
```sh
cmake -S bindings/nanobind -B bindings/nanobind/build
cmake --build bindings/nanobind/build -j
```

### 3c. Node / N-API — stock C++20
```sh
( cd bindings/node && npm install && npm run build )
```

### 3d. WASM / embind — stock emsdk (no fork)
```sh
emcmake cmake -S bindings/wasm -B bindings/wasm/build
cmake --build bindings/wasm/build -j
#   -> bindings/wasm/build/geom.js + geom.wasm, loadable in node/web
```

### 3e. Qt Widgets / QML — stock C++17 + Qt 6
```sh
# Qt prefix defaults to ~/Qt/6.8.3/macos; set it per-project with "qt_dir" in
# manifest.json, or override at configure time with -DQT_DIR=...
cmake -S bindings/qt  -B bindings/qt/build  && cmake --build bindings/qt/build  -j
cmake -S bindings/qml -B bindings/qml/build && cmake --build bindings/qml/build -j
#   -> ./bindings/qt/build/geom_qt    (QTabWidget, one inspector per class)
#   -> ./bindings/qml/build/geom_qml  (QtQuick inspector via the generic ReflectedObject)
```

`qt` includes only Qt + the (stock) user headers — no moc on the generated code.
`qml` reuses rosetta's generic `ReflectedObject` + `qml/Inspector.qml`, so moc
runs on that bridge only, never on per-type reflection.

### 3f. C# — native library (stock C++20) + .NET assembly
```sh
# reflection-free shim — builds with a stock compiler, no clang-p2996
cmake -S bindings/csharp -B bindings/csharp/build
cmake --build bindings/csharp/build -j
#   -> bindings/csharp/build/libcsgeom.{dylib,so}

# the C# wrappers (only needs dotnet)
dotnet build bindings/csharp/csgeom.csproj
```

```csharp
using csgeom;
using var t = new Triangle();
t.a = 2;                 // ok
t.a = -5;                // throws RosettaException — out-of-line range, enforced natively
System.Console.WriteLine(t.kind);   // enum, marshalled as its integer value
```

At run time the .NET loader must find `libcsgeom.*` (e.g.
`DYLD_LIBRARY_PATH=bindings/csharp/build` on macOS, `LD_LIBRARY_PATH=…` on Linux).

### 3g. Dear ImGui — inspector app (stock C++20, deps auto-fetched)
```sh
cmake -S bindings/imgui -B bindings/imgui/build
cmake --build bindings/imgui/build -j
./bindings/imgui/build/geom_imgui          # opens the inspector window
ROSETTA_IMGUI_FRAMES=5 ./bindings/imgui/build/geom_imgui  # smoke test
```

### 3h. Julia — jlcxx module (stock C++20 + CxxWrap.jl)
```sh
# needs Julia with CxxWrap installed:  julia -e 'using Pkg; Pkg.add("CxxWrap")'
cmake -S bindings/julia -B bindings/julia/build
cmake --build bindings/julia/build -j
#   -> bindings/julia/libjlgeom.dylib + jlgeom.jl (the loader module)
julia example_julia.jl
```

Unlike the thin `julia` target, `std::vector` is fully bound here (`getPoints`,
`getSurfaces`, vector-taking constructors — see the note in the main README).

### 3i. Lua — sol2 module (stock C++17 + Lua 5.1–5.4 / LuaJIT)
```sh
# sol2 is fetched automatically at configure time; on macOS `brew install lua@5.4`
# (sol2 does not support Lua 5.5 yet — the generated CMake prefers a 5.4 install)
cmake -S bindings/lua -B bindings/lua/build
cmake --build bindings/lua/build -j
#   -> bindings/lua/luageom.so — a require-able C module
```

```lua
local geom = require("luageom")
local s = geom.Surface.new({0,0,0, 1,0,0, 0,1,0}, {0,1,2})  -- tables in
s:transform(function(p) return geom.Point.new(p.x, p.z, p.y) end) -- Lua fn -> std::function
local t = geom.Triangle.new(1, 2, 3)
t.a = -5                              -- error: out-of-line range, enforced natively
```

## Run the examples

Each binding has a matching, self-contained script:

```sh
python3 example_pybind11.py   # python    (pygeom)
python3 example_nanobind.py   # nanobind  (nbgeom)

node    example_node.js       # node      (jsgeom — std::vector <-> JS Array)

node    example_wasm.js       # wasm      (geom — embind, async load + .delete())
python3 -m http.server 8000   # wasm running in a browser

DYLD_LIBRARY_PATH=bindings/csharp/build dotnet run --project run # csharp

# lua (luageom — tables, callbacks, range). Use the SAME Lua version
# the module was built against (Homebrew's plain `lua` is 5.5 — unsupported):
/opt/homebrew/opt/lua@5.4/bin/lua example_lua.lua

julia   example_julia.jl      # julia     (jlgeom — std::vector fully bound)
```

[`example_csharp.cs`](example_csharp.cs) covers the **csharp** target
(csgeom). C# needs the wrapper compiled in rather than imported, so the example's
header carries a short `dotnet run` recipe; it exercises the scalar / enum /
vector / range surface (object-graph methods stay on the node target — see the
boundary note in the file).

The wasm module also runs in the **browser** — [`example_wasm.html`](example_wasm.html)
loads it, exercises the same API, prints the results and draws the surface on a
`<canvas>`. Serve over HTTP (browsers can't `fetch` a `.wasm` from `file://`):

```sh
python3 -m http.server 8000
# then open http://localhost:8000/example_wasm.html
```

All four exercise the same `Model` / `Surface` / `Point` / `Triangle` API. The two
Python scripts are identical apart from the build dir and module name (rosetta
exposes the same API from pybind11 and nanobind). `example_node.js` and
`example_wasm.js` differ only in how each backend marshals `std::vector` and
manages object lifetime (see the header comment in each file).

Step 2 is the only one that needs the C++26/reflection toolchain, and it runs on
*your* machine. Step 3 — what a downstream consumer actually compiles — is
ordinary pybind11 / nanobind / N-API / embind.
