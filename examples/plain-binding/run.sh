#!/usr/bin/env bash
# One library, one pipeline, four language bindings.
#
#   ./run.sh          # generate + build, then the Python driver
#   ./run.sh node     # ...the Node one
#   ./run.sh julia    # ...the Julia one
#   ./run.sh wasm     # ...the WebAssembly one (needs an activated emsdk)
#   ./run.sh clean
#
# One stage, unlike ../scriptable-model: rosetta_gen emits the reflection
# driver, builds and runs it, then compiles each backend it produced. The
# generator needs clang-p2996; every generated binding builds with a stock
# toolchain, because series.h is plain C++20 and its annotations live out of
# line in Series.ann.json.
set -euo pipefail
cd "$(dirname "$0")"

GEN=../../bin/rosetta_gen

if [ "${1:-}" = "clean" ]; then
    $GEN --clean manifest.json >/dev/null 2>&1 || true
    rm -rf bindings gen generator
    echo "cleaned"
    exit 0
fi

echo ">> generate + build"
$GEN --build manifest.json

echo
case "${1:-}" in
    node)  node drive.js ;;
    julia) julia drive.jl ;;
    wasm)  node drive.wasm.js ;;
    *)     PYTHONPATH=bindings/python python3 drive.py ;;
esac
