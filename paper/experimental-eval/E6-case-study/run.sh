#!/usr/bin/env bash
# E6 -- the case study's central claim, made checkable.
#
# Section 11 argues that a library can grow a new class and every consumer
# acquires it without a binding change. That is easy to assert and easy to let
# rot, so it is checked here rather than described: the same library is put
# through the pipeline twice, once WITHOUT the solver and once WITH it, and the
# two sets of generated artifacts are compared.
#
# The prediction is asymmetric, and both halves matter:
#
#   metadata tables (stage 1)  MUST differ   -- otherwise the test passes
#                                               vacuously, having generated
#                                               nothing either time
#   scriptable binding (stage 2) MUST NOT    -- this is the claim. Stage 2 is a
#                                               binding of rosetta::script, not
#                                               of the library, so growing the
#                                               library must not move it by a
#                                               single byte.
#
# Only the generation stage runs; nothing here needs Python, Node, Qt or emsdk.
#
#   ./run.sh          # both arms + the comparison
#   ./run.sh clean
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
GEN="$ROOT/bin/rosetta_gen"
WORK="$HERE/work"
DYN="$ROOT/examples/dynamic"
SCR="$ROOT/examples/scriptable-model"

# The class the library grows. Removing it from the manifest is what makes the
# "before" arm: scene.h itself is IDENTICAL in both arms, so the comparison
# isolates "the binding surface grew" from any edit to the library's source.
NEW_CLASS="scene::Relaxer"

if [[ "${1:-}" == "clean" ]]; then
    rm -rf "$WORK" "$HERE/results"; echo "cleaned"; exit 0
fi
[[ -x "$GEN" ]] || { echo "error: $GEN not found; build tools/rosetta_gen first" >&2; exit 1; }

# Stage 1 (the library's metadata) then stage 2 (the binding of rosetta::script
# that never names a library class), into $WORK/$1.
build_arm() {
    local arm="$1"
    local drop="$2"
    local dir="$WORK/$arm"
    rm -rf "$dir"; mkdir -p "$dir"

    cp -R "$DYN"/*.h "$DYN"/*.json "$dir"/ 2>/dev/null || true
    # Drop the new class for the "before" arm, and absolutise the paths, which
    # in the original manifest are relative to the example's own directory.
    python3 - "$dir/manifest.json" "$drop" "$ROOT/include" "$dir" <<'PY'
import json, sys
path, drop, inc, here = sys.argv[1:5]
m = json.load(open(path))
if drop:
    n = len(m["classes"])
    m["classes"] = [c for c in m["classes"] if c.get("name") != drop]
    assert len(m["classes"]) == n - 1, f"{drop} is not in the manifest"
m["rosetta_include"] = inc
m["user_include"] = here
json.dump(m, open(path, "w"), indent=4)
PY

    ( cd "$dir"
      "$GEN" manifest.json gen > gen.log 2>&1
      cmake -S gen -B gen/build > configure.log 2>&1
      cmake --build gen/build > build.log 2>&1
      ./generator bindings > generate.log 2>&1 )

    # Stage 2: the scriptable binding, pointed at THIS arm's tables.
    local s2="$dir/scriptable"
    mkdir -p "$s2"
    python3 - "$SCR/manifest.json" "$s2/manifest.json" "$ROOT/include" "$dir" <<'PY'
import json, sys
src, dst, inc, arm = sys.argv[1:5]
m = json.load(open(src))
m["rosetta_include"] = inc
m["user_include"]  = [f"{arm}/bindings/dynamic", arm]
m["user_sources"]  = [f"{arm}/bindings/dynamic/auto_dynamic.cpp"]
json.dump(m, open(dst, "w"), indent=4)
PY
    ( cd "$s2"
      "$GEN" manifest.json gen > gen.log 2>&1
      cmake -S gen -B gen/build > configure.log 2>&1
      cmake --build gen/build > build.log 2>&1
      ./generator bindings > generate.log 2>&1 )
    echo "    $arm: ok"
}

mkdir -p "$WORK"
echo "=== before: the library WITHOUT $NEW_CLASS ================"
build_arm before "$NEW_CLASS"
echo "=== after:  the library WITH $NEW_CLASS ==================="
build_arm after ""

echo
echo "=== comparison ============================================"
python3 "$HERE/analyse.py" "$WORK" --new-class "$NEW_CLASS" --outdir "$HERE/results"
echo
cat "$HERE/results/results.md"
