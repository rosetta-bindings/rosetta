#!/usr/bin/env bash
# E3 -- generation cost and scalability.
#
# Sweeps the four size knobs of a synthetic library, one at a time from a common
# base point, and times every stage of the pipeline separately:
#
#   rosetta_gen      manifest -> driver project        (text templating)
#   cmake configure  toolchain probe                   (reported, not a per-library cost)
#   driver compile   the P2996 reflective walk         <- the hypothesis
#   generator run    IR -> emitted binding sources
#
# plus the size of what came out: emitted lines per target, and the compiled
# footprint of the `dynamic` metadata tables.
#
# Only the generation stage is used, with one exception that is deliberate: the
# metadata tables are compiled, because their .rodata size is a measurement and
# not an estimate. That compile uses the SYSTEM compiler -- the tables are stock
# C++20 -- so no extra toolchain is needed beyond clang-p2996 for the driver.
#
#   ./run.sh                    # every sweep
#   ./run.sh classes targets    # only those sweeps
#   ./run.sh clean
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
GEN="$ROOT/bin/rosetta_gen"
WORK="$HERE/work"
SYS_CXX="${CXX:-c++}"

if [[ "${1:-}" == "clean" ]]; then
    rm -rf "$WORK" "$HERE/results"; echo "cleaned"; exit 0
fi

[[ -x "$GEN" ]] || { echo "error: $GEN not found; build tools/rosetta_gen first" >&2; exit 1; }

# The base point. Every sweep varies exactly one of these and holds the rest.
BASE_C=16; BASE_M=4; BASE_F=3; BASE_R=1; BASE_T=3

# One configuration, end to end.
#   run_config <name> <classes> <methods> <fields> <overloads> <targets>
run_config() {
    local name="$1" c="$2" m="$3" f="$4" r="$5" t="$6"
    local dir="$WORK/$name"

    rm -rf "$dir"
    python3 "$HERE/genlib.py" --classes "$c" --methods "$m" --fields "$f" \
                              --overloads "$r" --targets "$t" --out "$dir"
    ( cd "$dir"
      local T="timing.json"
      python3 "$HERE/measure.py" "$T" rosetta_gen     -- "$GEN" manifest.json gen
      python3 "$HERE/measure.py" "$T" cmake_configure -- cmake -S gen -B gen/build
      python3 "$HERE/measure.py" "$T" driver_compile  -- cmake --build gen/build
      python3 "$HERE/measure.py" "$T" generator_run   -- ./generator bindings

      # The metadata tables' compiled footprint. Stock C++20, system compiler.
      if [[ -f bindings/dynamic/auto_dynamic.cpp ]]; then
          python3 "$HERE/measure.py" "$T" metadata_compile -- \
              "$SYS_CXX" -std=c++20 -O2 -c bindings/dynamic/auto_dynamic.cpp \
              -I"$ROOT/include" -Ilib -Ibindings/dynamic -o metadata.o
          # BSD size on Darwin, GNU size elsewhere; analyse.py parses either.
          { size -m metadata.o 2>/dev/null || size -A metadata.o 2>/dev/null || true; } \
              > metadata.size.txt
      fi
    )
}

WANTED=("$@")
[[ ${#WANTED[@]} -eq 0 ]] && WANTED=(classes methods fields overloads targets)

want() { for w in "${WANTED[@]}"; do [[ "$w" == "$1" ]] && return 0; done; return 1; }

mkdir -p "$WORK"

if want classes; then
    echo "=== sweep A: classes (methods=$BASE_M fields=$BASE_F overloads=$BASE_R targets=$BASE_T)"
    for c in 1 2 4 8 16 32 64 128; do
        run_config "A-c$c" "$c" "$BASE_M" "$BASE_F" "$BASE_R" "$BASE_T"
    done
fi

if want methods; then
    echo "=== sweep B: methods per class (classes=$BASE_C)"
    for m in 1 2 4 8 16; do
        run_config "B-m$m" "$BASE_C" "$m" "$BASE_F" "$BASE_R" "$BASE_T"
    done
fi

if want fields; then
    echo "=== sweep C: fields per class (classes=$BASE_C)"
    for f in 1 2 4 8 16; do
        run_config "C-f$f" "$BASE_C" "$BASE_M" "$f" "$BASE_R" "$BASE_T"
    done
fi

if want overloads; then
    echo "=== sweep D: overloads per name (classes=$BASE_C)"
    for r in 1 2 3 4 5; do
        run_config "D-r$r" "$BASE_C" "$BASE_M" "$BASE_F" "$r" "$BASE_T"
    done
fi

if want targets; then
    echo "=== sweep E: targets emitted (classes=$BASE_C, library fixed)"
    for t in 1 3 5 10 19; do
        run_config "E-t$t" "$BASE_C" "$BASE_M" "$BASE_F" "$BASE_R" "$t"
    done
fi

echo
echo "=== analysis ============================================="
python3 "$HERE/analyse.py" "$WORK" --outdir "$HERE/results"
echo
cat "$HERE/results/results.md"
