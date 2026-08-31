# dynamic-lib — binding a separately-compiled C++ shared library

Every other example binds a **header-only** library: the method bodies live in
the same headers rosetta reflects over, so the generated binding just `#include`s
them and needs nothing at link time. Real libraries aren't shaped that way — they
ship *declarations* in `include/` and *definitions* compiled into a shared object.

This example binds exactly that shape. It has two halves:

```
dynamic-lib/
├── space/                     a pure, plain-C++ library — NO rosetta, NO annotations
│   ├── include/               PUBLIC API: declarations only (Vector3.h, BoundingBox.h)
│   ├── src/                   the bodies (Vector3.cpp, BoundingBox.cpp)
│   ├── bin/                   -> libspace.dylib / .so  (build output)
│   └── CMakeLists.txt         builds the SHARED library
│
└── rosetta/                   the binding side
    ├── manifest.json          points at ../space, links against libspace (see "user_lib")
    ├── Vector3.ann.json       out-of-line docs (the space headers stay pristine)
    ├── BoundingBox.ann.json
    ├── example_python.py
    └── example_node.js
```

`space` is an ordinary library: `space/include/*.h` only **declare** `Vector3` /
`BoundingBox`; the bodies are in `space/src/*.cpp` and compiled into
`space/bin/libspace.*`. The headers carry no rosetta include and no annotations —
they build with any off-the-shelf compiler and sit in a real `namespace space`.

## The new manifest capability: `user_lib`

For a split library the generated binding must **link against** the shared object
to reach the out-of-line definitions. The manifest grew a field for that:

```json
"user_lib": {
    "name": "space",          // the library base name  (libspace.dylib / .so / .a)
    "dir":  "../space/bin",    // where it was built     (relative to the manifest)
    "link": "shared"          // "shared" (default) | "static" | "dynamic" (= shared)
}
```

### `link` — static or dynamic

`link` chooses which form of the library to link. The generated CMake resolves it
at configure time and references the library **by full path**, so the manifest —
not the linker's default — decides, and a same-named project target is never
linked by mistake:

```cmake
# preferred form first, then fall back to whichever is actually on disk
if(ROSETTA_USER_LIB_LINK_0 STREQUAL "static")
    set(_order "${_static}" "${_shared}")   # lib<name>.a, then lib<name>.dylib/.so
else()
    set(_order "${_shared}" "${_static}")
endif()
# first existing wins; if neither is built yet, fall back to -l<name>
```

So:

- **`"shared"`** (default) → links `libspace.dylib` / `.so` (+ rpath). Falls back to
  the static archive if no shared library exists.
- **`"static"`** → links `libspace.a` directly into the module. Falls back to the
  shared library if no archive exists.
- **`"dynamic"`** is accepted as an alias for `"shared"`.
- **WebAssembly ignores `link` and is always static** — a native `.dylib`/`.so`
  cannot enter a wasm module and there is no rpath. The wasm backend links
  `lib<name>.a` (built with the same emsdk) directly.

The resolver prints which file it picked at configure time:

```
-- rosetta: space links user library .../space/bin/libspace.dylib (requested shared)
```

This wiring is emitted for each C++ binding backend (the reflection `python` /
`node` / … projects and their `*-expanded` variants) **and** for the generator
driver itself (the reflection walk instantiates each bound type, so it needs the
definitions at link time too). Without `user_lib` the block is skipped and the
project stays header-only, exactly as before.

### Several libraries

`space` depends on nothing, but a real library usually does. `user_lib` also
takes an **array** of the same objects — the bound library first, then the
pre-built ones it needs, in link order:

```json
"user_lib": [
    { "name": "space", "dir": "../space/bin", "link": "shared" },
    { "name": "foo",   "dir": "/opt/foo/lib", "link": "static" }
]
```

Each entry is resolved by the same rules and keeps its own `link`; every distinct
directory joins the binding's rpath. See
[docs/MANIFEST.md](../../docs/MANIFEST.md#linking-external-libraries-user_lib). See the field documented in
[`tools/rosetta_gen/rosetta_gen.cpp`](../../tools/rosetta_gen/rosetta_gen.cpp) and
plumbed through [`include/rosetta/generate.h`](../../include/rosetta/generate.h).

### Namespaces

`space::Vector3` lives in a namespace, but rosetta's C++ backends spell bound
types by their *unqualified* identifier (`bind_pybind<Vector3>`,
`py::class_<Vector3>`, …). So the generator derives each type's enclosing
namespace by reflection and emits a `using namespace space;` into each generated
TU — automatic, nothing to declare. It lands right after the user headers, ahead
of the embedded annotation specializations and the binding code that all spell
the type unqualified:

```cpp
// auto_pybind.cpp (generated)
#include "Vector3.h"
using namespace space;            // <- emitted from reflection
PYBIND11_MODULE(space, m) { rosetta::bind_pybind<Vector3>(m, "Vector3"); ... }
```

## Reproduce

The one-shot script runs the whole pipeline (see the per-step commands inside):

```sh
./build.sh
```

Or by hand, from this directory:

```sh
# 1. the pure-C++ shared library (off-the-shelf compiler — no rosetta)
cmake -S space -B space/build && cmake --build space/build -j     # -> space/bin/libspace.*

# 2. scaffold + build the generator driver (needs clang-p2996)
( cd rosetta && ../../bin/rosetta_gen manifest.json gen )
cmake -S rosetta/gen -B rosetta/gen/build && cmake --build rosetta/gen/build -j

# 3. run it -> rosetta/bindings/{python,node,wasm}
( cd rosetta && ./generator bindings )

# 4a. python (reflection binding; CMake selects clang-p2996), linked to libspace
cmake -S rosetta/bindings/python -B rosetta/bindings/python/build
cmake --build rosetta/bindings/python/build -j

# 4b. node (reflection binding; CMake selects clang-p2996), linked to libspace
( cd rosetta/bindings/node && npm install && npm run build )
```

Then run the examples (the bindings' rpath points at `space/bin`, so
`libspace.*` is found wherever you run from):

```sh
cd rosetta
python3 example_python.py
node    example_node.js
```

```
Vector3(3, 0, 4).length() = 5.0          # body resolved from libspace.dylib
a.cross(b) = (-3.0, 6.0, -3.0)
box.center()   = (1.0, 1.0, 1.0)
doc(Vector3.x) = The x-coordinate (first Cartesian axis)   # out-of-line annotation
```

## WebAssembly

`wasm` is generated with the same `user_lib` wiring, but a native
`.dylib`/`.so` **cannot** be linked into WebAssembly. So `space` is built a
second time as a *wasm static archive* with the **same emsdk** — its
`CMakeLists.txt` emits a `STATIC` library under `EMSCRIPTEN`, producing
`space/bin/libspace.a` right next to the native `libspace.dylib`. The two
coexist; each toolchain's linker picks the form it can use, so `user_lib.dir`
already points at the right place (`space/bin`) — nothing to repoint.

> **Note.** This `libspace.a` is the *wasm* archive. Because the native targets
> here use `link: "shared"`, they resolve `libspace.dylib` and never touch it. If
> you instead want native **`link: "static"`**, build a *native* `libspace.a` into
> a **separate** directory and point `user_lib.dir` there — a native and a wasm
> archive of the same name cannot share one directory.

`build.sh` runs this only when `emcmake` is on `PATH`:

```sh
# build space as a wasm static archive (libspace.a) with the same emsdk
emcmake cmake -S space -B space/build-wasm && cmake --build space/build-wasm
# then the embind module — links libspace.a statically into space.wasm
emcmake cmake -S rosetta/bindings/wasm -B rosetta/bindings/wasm/build
cmake --build rosetta/bindings/wasm/build

cd rosetta && node example_wasm.js
```

```
Vector3(3, 0, 4).length() = 5            # body resolved from libspace.a (baked into space.wasm)
a.cross(b) = [ -3, 6, -3 ]
box.center()   = [ 1, 1, 1 ]
```

Because the wasm module target is itself named `space` (the manifest target),
the generated link line uses an explicit `-l` flag —
`target_link_libraries(space PRIVATE "-l${ROSETTA_USER_LIB}")` — so CMake links
the external archive by file name instead of mistaking it for the same-named
module target.
