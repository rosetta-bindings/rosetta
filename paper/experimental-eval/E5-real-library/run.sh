#!/usr/bin/env bash
# E5 -- coverage on a real, unmodified third-party library.
#
# Two arms over the SAME library, emitting the SAME targets, differing only in
# who chose what to bind:
#
#   A  exhaustive  every class, enum and free function the compiler finds in the
#                  namespace. No selection, no tuning, no author.
#   B  curated     an existing hand-written manifest, verbatim, retargeted only
#                  so both arms emit the same list.
#
# Only the generation stage runs: coverage.json is written before anything is
# compiled, so no Python / Node / JDK / emsdk toolchain is involved. What IS
# needed is a system C++ compiler for the enumeration step (clang parses the
# library's headers to establish the frame) and clang-p2996 for the drivers.
#
#   ./run.sh              # both arms
#   ./run.sh exhaustive   # one arm
#   ./run.sh clean
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
GEN="$ROOT/bin/rosetta_gen"
WORK="$HERE/work"

# --- the subject -----------------------------------------------------------
# PMP (https://github.com/pmp-library/pmp-library), unmodified, as vendored by
# an existing rosetta project. Chosen over geogram because it is not a library
# any author of this paper wrote, and because it is small enough to bind
# EXHAUSTIVELY -- which is what lets arm A have no author at all.
PMP_ROOT="${PMP_ROOT:-$HOME/devs/c++/pmp-rosetta}"
PMP_SRC="$PMP_ROOT/extern/pmp/src"
PMP_EIGEN="$PMP_ROOT/extern/pmp/external/eigen-3.4.0"
CURATED_MANIFEST="${CURATED_MANIFEST:-$PMP_ROOT/manifest.json}"

TARGETS="python,nanobind,julia,lua,node,wasm,csharp,java,typescript,dynamic"

if [[ "${1:-}" == "clean" ]]; then
    rm -rf "$WORK" "$HERE/results"; echo "cleaned"; exit 0
fi

[[ -x "$GEN" ]] || { echo "error: $GEN not found; build tools/rosetta_gen first" >&2; exit 1; }
[[ -d "$PMP_SRC" ]] || { echo "error: PMP sources not at $PMP_SRC (set PMP_ROOT)" >&2; exit 1; }

WANTED=("$@")
[[ ${#WANTED[@]} -eq 0 ]] && WANTED=(exhaustive curated)
want() { for w in "${WANTED[@]}"; do [[ "$w" == "$1" ]] && return 0; done; return 1; }

mkdir -p "$WORK"

# Generate, compile the driver, run it. The driver compile is where a class the
# walk cannot handle would surface -- as a BUILD failure, which is exactly the
# outcome section 9's "skip, don't break" claim says should not happen. So it is
# not swallowed: a non-zero rc here fails the run and is the finding.
run_arm() {
    local dir="$1" label="$2"
    ( cd "$dir"
      "$GEN" manifest.json gen > gen.log 2>&1
      cmake -S gen -B gen/build > configure.log 2>&1
      cmake --build gen/build > build.log 2>&1 \
          || { echo "    $label: DRIVER COMPILE FAILED -- see $dir/build.log" >&2; exit 1; }
      ./generator bindings > generate.log 2>&1
      echo "    $label: ok" )
}

# --- the frame: what the library declares, per the compiler ----------------
echo "=== frame ================================================="
python3 "$HERE/enumerate.py" \
    --src "$PMP_SRC" --header-dir pmp --namespace pmp \
    --include "$PMP_EIGEN" --exclude-dir viewers \
    --out "$WORK/frame.json"

if want exhaustive; then
    echo "=== arm A: exhaustive ====================================="
    rm -rf "$WORK/exhaustive"; mkdir -p "$WORK/exhaustive"
    python3 "$HERE/genmanifest.py" \
        --frame "$WORK/frame.json" --out "$WORK/exhaustive/manifest.json" \
        --targets "$TARGETS" --module pmp \
        --user-include "$PMP_SRC" --user-include "$PMP_EIGEN" \
        --rosetta-include "$ROOT/include"
    run_arm "$WORK/exhaustive" "exhaustive"
fi

if want curated; then
    echo "=== arm B: curated ========================================"
    rm -rf "$WORK/curated"; mkdir -p "$WORK/curated"
    python3 "$HERE/retarget.py" \
        --manifest "$CURATED_MANIFEST" --out "$WORK/curated/manifest.json" \
        --targets "$TARGETS" --module pmp \
        --user-include "$PMP_SRC" --user-include "$PMP_EIGEN" \
        --rosetta-include "$ROOT/include"
    run_arm "$WORK/curated" "curated"
fi

echo
echo "=== analysis =============================================="
python3 "$HERE/analyse.py" "$WORK" --outdir "$HERE/results"
echo
cat "$HERE/results/results.md"
