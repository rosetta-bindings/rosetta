#!/usr/bin/env bash
# Build the whole `dynamic-lib` example end-to-end, from this directory:
#
#   ./build.sh
#
# Pipeline:
#   1. space/      — the pure-C++ library  -> space/bin/libspace.dylib  (stock compiler)
#   2. rosetta/gen — scaffold + build the generator driver               (needs clang-p2996)
#   3. ./generator — emit the per-backend binding projects               (reflection runs here)
#   4. python + node — reflection bindings, built and linked to libspace (clang-p2996, set
#                      automatically by each generated CMakeLists)
#   5. wasm — built only if a stock emsdk (emcmake) is on PATH
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"            # rosetta repo root
ROSETTA_GEN="$ROOT/bin/rosetta_gen"

# --- 1. the pure-C++ shared library (no rosetta, no reflection) ---
echo ">> [1/5] building space/ -> bin/libspace.*"
cmake -S space -B space/build
cmake --build space/build -j

# --- 2. scaffold + build the generator driver (clang-p2996) ---
echo ">> [2/5] scaffolding + building the generator driver"
( cd rosetta && "$ROSETTA_GEN" manifest.json gen )
cmake -S rosetta/gen -B rosetta/gen/build
cmake --build rosetta/gen/build -j

# --- 3. run the driver -> bindings/{python,node,wasm} ---
echo ">> [3/5] generating the binding projects"
( cd rosetta && ./generator bindings )

# --- 4. python + node (reflection bindings), linked against libspace ---
echo ">> [4/5] building python (reflection)"
cmake -S rosetta/bindings/python -B rosetta/bindings/python/build
cmake --build rosetta/bindings/python/build -j

echo ">> [4/5] building node (reflection)"
( cd rosetta/bindings/node && npm install && npm run build )

# --- 5. wasm (only if a stock emsdk is available) ---
if command -v emcmake >/dev/null 2>&1; then
    echo ">> [5/5] building wasm (stock emsdk)"
    echo "   NOTE: wasm cannot link a native .dylib — first rebuild space as a wasm"
    echo "   static archive (libspace.a) with the SAME emsdk; the wasm binding's"
    echo "   user_lib.dir already points at space/bin where it lands."
    emcmake cmake -S space -B space/build-wasm           # space/CMakeLists builds STATIC under EMSCRIPTEN
    cmake --build space/build-wasm -j                    # -> space/bin/libspace.a
    emcmake cmake -S rosetta/bindings/wasm -B rosetta/bindings/wasm/build
    cmake --build rosetta/bindings/wasm/build -j
else
    echo ">> [5/5] skipping wasm (no emsdk: 'emcmake' not on PATH)"
    echo "   The project is generated under rosetta/bindings/wasm/."
fi

echo ""
echo ">> done. Try:"
echo "     ( cd rosetta && python3 example_python.py )"
echo "     ( cd rosetta && node    example_node.js   )"
