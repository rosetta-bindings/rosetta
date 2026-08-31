#!/usr/bin/env bash
# E4 -- runtime overhead.
#
# Unlike E1/E2 this one has to BUILD and RUN, so it needs more than the
# generator: a C++ compiler for layer A, and Python + pybind11 for layer B.
# Layer A alone is still useful and has no Python dependency; pass --cpp-only.
#
#   ./run.sh                # both layers
#   ./run.sh --cpp-only     # layer A only (no Python needed)
#   ./run.sh clean
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
GEN="$ROOT/bin/rosetta_gen"
WORK="$HERE/work"
ITERS=${E4_ITERS:-200000}
PYNUM=${E4_PYNUM:-20000}

[[ "${1:-}" == "clean" ]] && { rm -rf "$WORK" "$HERE/results"; echo cleaned; exit 0; }
CPP_ONLY=0
[[ "${1:-}" == "--cpp-only" ]] && CPP_ONLY=1

[[ -x "$GEN" ]] || { echo "error: $GEN not found; build tools/rosetta_gen first" >&2; exit 1; }

generate() {   # generate() <dir>
    ( cd "$1"
      "$GEN" manifest.json gen >/dev/null
      cmake -S gen -B gen/build > gen/configure.log 2>&1
      cmake --build gen/build -j > gen/build.log 2>&1
      ./generator bindings > gen/run.log 2>&1 )
}

echo "=== generating ==========================================="
rm -rf "$WORK"
python3 "$HERE/genlib.py" --out "$WORK"
generate "$WORK/dyn";    echo "    metadata tables"
if [[ $CPP_ONLY -eq 0 ]]; then
    generate "$WORK/static"; echo "    pybind11 module"
    generate "$WORK/meta";   echo "    scriptable module"
fi

echo
echo "=== layer A: C++ ========================================="
# -O2 explicitly: the benchmark must not be measuring an unoptimised build.
c++ -std=c++20 -O2 -DNDEBUG \
    -I "$WORK/lib" -I "$WORK/dyn/bindings/dynamic" -I "$ROOT/include" \
    "$HERE/bench_cpp.cpp" "$WORK/dyn/bindings/dynamic/auto_dynamic.cpp" \
    -o "$WORK/bench_cpp"
"$WORK/bench_cpp" "$ITERS" "$WORK/cpp.csv"

CSVS=("$WORK/cpp.csv")

if [[ $CPP_ONLY -eq 0 ]]; then
    echo
    echo "=== layer B: Python ======================================"
    # CMAKE_BUILD_TYPE is EMPTY by default in the generated CMakeLists, which
    # yields an unoptimised module and numbers roughly 5x too slow. Release is
    # passed explicitly here; see the README.
    for arm in static meta; do
        cmake -S "$WORK/$arm/bindings/python" -B "$WORK/$arm/bindings/python/build" \
              -DCMAKE_BUILD_TYPE=Release > "$WORK/$arm/pyconf.log" 2>&1
        cmake --build "$WORK/$arm/bindings/python/build" -j > "$WORK/$arm/pybuild.log" 2>&1
    done
    python3 "$HERE/bench_py.py" \
        --static-dir "$WORK/static/bindings/python" \
        --meta-dir   "$WORK/meta/bindings/python" \
        --number "$PYNUM" --out "$WORK/py.csv"
    CSVS+=("$WORK/py.csv")
fi

echo
echo "=== analysis ============================================="
python3 "$HERE/analyse.py" "${CSVS[@]}" --outdir "$HERE/results"
echo
cat "$HERE/results/results.md"
