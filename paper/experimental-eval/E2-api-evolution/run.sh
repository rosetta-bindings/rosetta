#!/usr/bin/env bash
# E2 -- the cost of API evolution.
#
# Grows one library across three versions and measures, for each arm, how much
# of the binding has to change. Only the GENERATION stage is used: file counts,
# line counts and diffs are all decidable before anything is compiled, so no
# Python/Lua/Node toolchain is needed -- just clang-p2996.
#
#   ./run.sh              # v1=10, v2=30, v3=130 classes
#   ./run.sh 5 10 20      # custom class counts
#   ./run.sh clean
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
GEN="$ROOT/bin/rosetta_gen"
WORK="$HERE/work"

if [[ "${1:-}" == "clean" ]]; then
    rm -rf "$WORK" "$HERE/results"; echo "cleaned"; exit 0
fi

[[ -x "$GEN" ]] || { echo "error: $GEN not found; build tools/rosetta_gen first" >&2; exit 1; }

SIZES=("$@")
[[ ${#SIZES[@]} -eq 0 ]] && SIZES=(10 30 130)

# Run one arm: rosetta_gen -> cmake -> build driver -> emit bindings.
# Records wall-clock for the driver compile and the generation separately.
run_arm() {
    local dir="$1" label="$2"
    ( cd "$dir"
      "$GEN" manifest.json gen >/dev/null
      cmake -S gen -B gen/build > gen/configure.log 2>&1
      local t0 t1 t2
      t0=$(date +%s)
      cmake --build gen/build -j > gen/build.log 2>&1
      t1=$(date +%s)
      ./generator bindings > gen/run.log 2>&1
      t2=$(date +%s)
      printf '{"driver_compile_s": %d, "generate_s": %d}\n' $((t1-t0)) $((t2-t1)) > timing.json
      echo "    $label: compile $((t1-t0))s  generate $((t2-t1))s"
    )
}

VERSIONS=()
i=0
for N in "${SIZES[@]}"; do
    i=$((i+1))
    V="$WORK/v$i"
    VERSIONS+=("$V")
    echo "=== v$i: $N classes ==========================================="
    rm -rf "$V"
    python3 "$HERE/genlib.py" --classes "$N" --out "$V"

    run_arm "$V/static" "static     "
    run_arm "$V/dyn"    "dyn  (s1)  "
    # Stage 2 must run after stage 1: its manifest points at the tables stage 1
    # emitted. Nothing about its CONTENT depends on them, which is the point.
    run_arm "$V/meta"   "meta (s2)  "
done

echo
echo "=== analysis ============================================="
python3 "$HERE/analyse.py" "${VERSIONS[@]}" --outdir "$HERE/results"
echo
cat "$HERE/results/results.md"
