#!/usr/bin/env bash
# Build and drive the scriptable object model, end-to-end from this directory:
#
#   ./run.sh          # both stages, then the Python driver
#   ./run.sh lua      # ...the Lua one
#   ./run.sh node     # ...the Node one
#   ./run.sh clean    # remove what THIS example generated (stage 2 only)
#
# Two stages, because they are two different jobs — and stage 1 lives in
# another example, which is the point: this one binds rosetta's reflection API
# and never names a scene type.
#
#   1. ../dynamic — the `dynamic` backend turns scene.h into
#                   ../dynamic/bindings/dynamic/auto_dynamic.{h,cpp}. Those two
#                   files are the entire interface between the two folders, and
#                   they are NOT checked in, so a fresh clone must generate
#                   them. Needs clang-p2996.
#   2. here       — binds <rosetta/script.h> and compiles those tables in.
#                   Stock compiler for the target; the generator needs p2996.
#
# Stage 1 is skipped when its output already exists, so re-runs are quick.
# See minimal/ for the same thing on a library that has nothing to do with
# rosetta, with both stages inside one directory.
set -euo pipefail
cd "$(dirname "$0")"

GEN=../../bin/rosetta_gen

if [ "${1:-}" = "clean" ]; then
    $GEN --clean manifest.json >/dev/null 2>&1 || true
    rm -rf bindings gen generator
    echo "cleaned (../dynamic left alone — clean it there)"
    exit 0
fi

# --- 1. the scene library -> metadata tables ---
if [ ! -f ../dynamic/bindings/dynamic/auto_dynamic.cpp ]; then
    echo ">> stage 1: ../dynamic/scene.h -> auto_dynamic.{h,cpp}"
    (cd ../dynamic && ../../bin/rosetta_gen --build manifest.json)
fi

# --- 2. the reflection API -> python / lua / node / ... modules ---
echo ">> stage 2: <rosetta/script.h> -> rosetta_meta"
$GEN --build manifest.json

# --- 3. drive it ---
echo
case "${1:-}" in
    lua)  (cd bindings/lua && lua ../../drive.lua) ;;
    node) node drive.js ;;
    *)    PYTHONPATH=bindings/python python3 drive.py ;;
esac
