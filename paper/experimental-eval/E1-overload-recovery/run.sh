#!/usr/bin/env bash
# E1 -- overload recovery.
#
# Sweeps the number of overloads per method name and records, for every target,
# how many methods actually bound. Only the GENERATION stage is needed:
# coverage.json is written before anything is compiled, so no emsdk, JDK, .NET,
# Julia or Lua has to be installed -- just clang-p2996 for the driver.
#
#   ./run.sh              # full sweep (overloads 1..5)
#   ./run.sh 1 2 3        # only those multiplicities
#   ./run.sh clean
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
GEN="$ROOT/bin/rosetta_gen"
WORK="$HERE/work"

CLASSES=${E1_CLASSES:-8}
NAMES=${E1_NAMES:-6}

if [[ "${1:-}" == "clean" ]]; then
    rm -rf "$WORK" "$HERE/results"
    echo "cleaned"
    exit 0
fi

if [[ ! -x "$GEN" ]]; then
    echo "error: $GEN not found. Build it first:" >&2
    echo "  cmake -G Ninja -S $ROOT/tools/rosetta_gen -B $ROOT/tools/rosetta_gen/build" >&2
    echo "  cmake --build $ROOT/tools/rosetta_gen/build" >&2
    exit 1
fi

MULTIPLICITIES=("$@")
if [[ ${#MULTIPLICITIES[@]} -eq 0 ]]; then
    MULTIPLICITIES=(1 2 3 4 5)
fi

CONFIGS=()
for R in "${MULTIPLICITIES[@]}"; do
    CFG="$WORK/o$R"
    CONFIGS+=("$CFG")
    echo "=== overloads per name: $R ==============================="

    rm -rf "$CFG"
    python3 "$HERE/genlib.py" --classes "$CLASSES" --names "$NAMES" \
                              --overloads "$R" --out "$CFG"

    ( cd "$CFG"
      "$GEN" manifest.json gen >/dev/null
      cmake -S gen -B gen/build              > gen/configure.log 2>&1
      t0=$(date +%s)
      cmake --build gen/build -j             > gen/build.log 2>&1
      t1=$(date +%s)
      ./generator bindings                   > gen/run.log 2>&1
      t2=$(date +%s)
      echo "    driver compile: $((t1-t0))s   generate: $((t2-t1))s"
      printf '{"driver_compile_s": %d, "generate_s": %d}\n' \
             $((t1-t0)) $((t2-t1)) > timing.json
    )
done

echo
echo "=== analysis ============================================="
python3 "$HERE/analyse.py" "${CONFIGS[@]}" --outdir "$HERE/results"
echo
cat "$HERE/results/results.md"
