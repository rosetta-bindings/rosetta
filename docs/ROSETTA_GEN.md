# `rosetta_gen` — the framework command-line tool

`rosetta_gen` is the single binary that drives the whole rosetta pipeline. It reads a [`manifest.json`](MANIFEST.md) and can take you from "a folder of C++ headers" to "compiled bindings for every declared language" in one command — or one step at a time, when you want to inspect what each stage produces.

The tool itself uses **no reflection** — only JSON parsing and text templating — so it builds with a stock C++17 compiler. Only the *generator* it emits (and the thin backends) need the clang-p2996 toolchain.

---

## Building the tool

One-time, from the repo root (CMake ≥ 3.28; fetches the single-header nlohmann/json automatically):

```bash
cmake -G Ninja -S tools/rosetta_gen -B tools/rosetta_gen/build
cmake --build tools/rosetta_gen/build
```

The binary always lands in **`<repo>/bin/rosetta_gen`** (not the build tree). If you scaffolded your project with [`tools/rosetta_init.py`](../tools/rosetta_init.py), its bootstrap `CMakeLists.txt` does this for you — the binary appears at `extern/rosetta/bin/rosetta_gen`.

---

## The four modes

```
rosetta_gen <manifest.json> [out_dir]        # plain: emit the generator project only
rosetta_gen --build <manifest.json> [opts]   # the whole pipeline in one command
rosetta_gen --clean <manifest.json> [opts]   # remove everything generated
rosetta_gen --init  [manifest.json] [src]    # write a starter manifest
```

`rosetta_gen --help` prints the full built-in help; `--build --help` and `--clean --help` list each mode's options.

How they chain together:

```
--init ──► manifest.json ──► --build ─────────────────────► bindings/python  node  wasm …
 (start)    (you edit it)      │                                   ▲
                               │ = the three manual steps:         │
                               │   1. emit gen/ (plain mode)       │
                               │   2. cmake gen/  → run generator ─┘
                               │   3. cmake / npm / emcmake each backend
--clean ◄── removes gen/ (or generated/), the generator binary and bindings/
```

---

## `--init` — start a project

Writes a starter `manifest.json` (default `./manifest.json`; **never overwrites** an existing file — remove it first to regenerate).

### Case 1: blank, commented manifest

```bash
bin/rosetta_gen --init
```

Emits a fully-commented example exercising every commonly-used field (`cpp26_*` toolchain overrides, multi-entry `user_include`, `user_sources`, `compile_definitions`, `build_type` / `optimization`, a representative spread of targets, one class, one function) — delete what you don't need rather than hunting the docs for what exists.

### Case 2: pre-filled from a source scan

```bash
bin/rosetta_gen --init manifest.json ./src
```

An argument naming an existing **directory** is the source tree to scan; the other (if any) is the manifest path. The scan walks `./src` for headers (`.h/.hh/.hpp/.hxx`) and sources (`.cpp/.cxx/.cc`) and pre-fills:

- `classes` / `functions` — from the declarations found, with shared `namespace` / `header_dir` defaults factored out when every entry agrees;
- `user_sources` — every source file found, **except** any defining `main()` (it would clash with the binding modules' entry points — reported as a note);
- `rosetta_include` — guessed by walking up from the `rosetta_gen` binary (it usually runs from inside a rosetta checkout); a `FIXME` comment otherwise.

The scan is a comment/string-aware token scan, **not a real C++ parse**: template classes, overloaded free functions (an overload set cannot be bound) and anonymous namespaces are skipped, each with a printed note. VCS dirs, build trees and vendored code (`build/`, `extern/`, `third_party/`, `node_modules/`, …) are not descended into. **Review the result before building.**

> For a whole *project* skeleton (bootstrap `CMakeLists.txt` that fetches rosetta into `extern/`, `.gitignore`, `README.md`) rather than just the manifest, use [`tools/rosetta_init.py`](../tools/rosetta_init.py) — see the [minimal example](MANIFEST.md#minimal-example).

---

## `--build` — the whole pipeline in one command

```bash
bin/rosetta_gen --build manifest.json
```

runs, in order:

1. **emit** the generator project into `<manifest dir>/gen/` (`bindings.h`, `<generator_name>.cpp`, `CMakeLists.txt`);
2. **build** it with CMake (this step needs the clang-p2996 toolchain — reflection runs here) and **run** the resulting generator, writing one self-contained project per target into `<manifest dir>/bindings/<lang>/`;
3. **compile every backend** the manifest declares, each with its own build shape:

| Backend | Build |
|---|---|
| `python`, `nanobind`, `rest`, `json`, `lua-expanded`, `imgui-expanded`, … | plain `cmake` configure + build |
| `node`, `node-expanded` | `npm install` + `npm run build` |
| `wasm`, `wasm-expanded` | `emcmake cmake` + `cmake --build` |
| `julia`, `julia-expanded` | `cmake` (locates JlCxx by running `julia`) |
| `csharp`, `csharp-expanded` | `cmake`, then `dotnet build` as a second stage |
| `java`, `java-expanded` | `cmake`, then `mvn package` as a second stage |
| `qt-expanded`, `qml-expanded` | `cmake` with `-DQT_DIR` (see `--qt-dir`) |
| `markdown`, `html`, `typescript`, `openapi`, `paraview` | nothing to compile |

A backend whose toolchain is **missing** (no `npm`, no `emcc`, no `julia`…) is *skipped with a note*, not an error — likewise `dotnet` / `mvn` absence downgrades csharp / java to "OK (native only)". A backend whose build **fails** is reported and `--build` exits non-zero *after trying the rest*. Every run ends with a per-backend summary:

```
== summary
   python-expanded      OK
   node-expanded        OK
   wasm-expanded        SKIPPED (emcc not found — activate emsdk)
   markdown             OK (nothing to compile)
```

### Options

```
--gen-dir DIR            generator project dir   (default: <manifest dir>/gen)
--bindings-dir DIR       generated bindings dir  (default: <manifest dir>/bindings)
--clang-p2996-root PATH  -DCLANG_P2996_ROOT for every CMake configure
--qt-dir PATH            -DQT_DIR for the qt-/qml-expanded builds
--only a,b / --skip a,b  restrict the backend builds to / exclude these
--cmake-arg ARG          extra argument for every CMake configure (repeatable)
--jobs N                 parallel build jobs
--fresh                  wipe the gen and bindings dirs first
```

### Case 3: everything, default layout

```bash
bin/rosetta_gen --build examples/generate/manifest.json
# → examples/generate/gen/       (generator project + build tree)
# → examples/generate/bindings/  (one folder per backend, compiled)
```

### Case 4: iterate on one or two backends

While developing you rarely want the full matrix. `--only` / `--skip` filter **which backends get compiled** (generation still emits them all — the filters name the `lang` values):

```bash
# just python and node, 8-way parallel
bin/rosetta_gen --build manifest.json --only python-expanded,node-expanded --jobs 8

# everything except the slow wasm link
bin/rosetta_gen --build manifest.json --skip wasm-expanded
```

### Case 5: thin backends with a custom reflection toolchain

The thin (non-`-expanded`) backends re-run reflection when *they* compile, so every CMake configure needs to know where the clang-p2996 build lives if it isn't at the manifest's `cpp26_root` default:

```bash
bin/rosetta_gen --build manifest.json \
    --clang-p2996-root ~/devs/clang-p2996/build --fresh
```

`--fresh` wipes `gen/` and `bindings/` first — the right move after toolchain or manifest-layout changes, when stale CMake caches would otherwise bite.

### Case 6: Qt / QML inspectors

```bash
bin/rosetta_gen --build manifest.json \
    --only qt-expanded,qml-expanded --qt-dir ~/Qt/6.8.3/macos
```

(Equivalently, set `qt_dir` in the manifest itself and drop the flag.)

### Case 7: passing flags to every CMake configure

`--cmake-arg` is repeatable and applies to the generator's configure **and** every backend's (including the emcmake configure for wasm):

```bash
bin/rosetta_gen --build manifest.json \
    --cmake-arg -DCMAKE_BUILD_TYPE=Debug \
    --cmake-arg -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

---

## Plain mode — one step at a time

```bash
bin/rosetta_gen manifest.json [out_dir]
```

emits **only** the generator project (default `out_dir`: `<manifest dir>/generated/`) and stops — you drive the remaining steps yourself. Useful for inspecting `bindings.h`, hacking on the emitted driver, or wiring the steps into your own build system:

```bash
# 1. manifest → generator project
bin/rosetta_gen manifest.json gen

# 2. build the generator (clang-p2996) and run it
cmake -S gen -B gen/build && cmake --build gen/build
./generator bindings          # the binary is dropped NEXT TO the project dir

# 3. build any backend by hand
cmake -S bindings/python -B bindings/python/build
cmake --build bindings/python/build
```

Two footguns the tool warns about:

- a fresh checkout (or a `--clean`) leaves `out_dir/build` unconfigured — `cmake --build` alone fails, so the *configure* step is printed as a `next:` hint;
- if the emitted sources changed but a generator binary from a previous emit still sits next to `out_dir`, running it without rebuilding **silently re-emits the old bindings** — the tool prints a rebuild note when it detects this.

Re-emitting is idempotent: unchanged files are not rewritten, so the next `cmake --build` recompiles nothing.

---

## `--clean` — back to sources

```bash
bin/rosetta_gen --clean manifest.json [--gen-dir DIR] [--bindings-dir DIR]
```

Removes everything the pipeline generated for this manifest:

- the generator project dir — the explicit `--gen-dir`, or **both** defaults (`gen/` from `--build` and `generated/` from plain mode);
- the generator binary dropped beside it (including per-config subdirs of multi-config generators);
- the per-backend folders under `bindings/` **that the manifest declares** — anything else you parked there survives, and the `bindings/` dir itself is only removed once empty.

Never the manifest itself — and never a folder whose `CMakeLists.txt` lacks the `Generated by rosetta` stamp the emitters write, so a hand-written folder sharing a name survives:

```
not removing gen (no rosetta_gen marker in its CMakeLists)
```

---

## Exit status

| Mode | `0` | non-zero |
|---|---|---|
| plain | project emitted | manifest missing / invalid, emit error |
| `--build` | every *attempted* backend built (skips don't count) | generator build/run failed, or ≥ 1 backend build failed |
| `--clean` | clean ran (nothing to remove is fine) | manifest missing / invalid |
| `--init` | manifest written | target file already exists, bad arguments |

---

## See also

- [MANIFEST.md](MANIFEST.md) — every manifest field, with the minimal example.
- [QUICKSTART.md](QUICKSTART.md) — the full first-time walkthrough.
- [EXTENDING_BACKEND.md](EXTENDING_BACKEND.md) — writing a custom backend plugin (compiled into the generator via the manifest's `plugins`).
- [`tools/rosetta_init.py`](../tools/rosetta_init.py) — scaffold a whole binding project (bootstrap CMake + manifest skeleton) around `rosetta_gen`.
