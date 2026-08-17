#!/usr/bin/env bash
# The smallest complete path from a stock-C++ library to a scriptable object
# model, end-to-end from this directory:
#
#   ./run.sh          # both stages, then the Python driver
#   ./run.sh lua      # both stages, then the Lua driver
#   ./run.sh node     # both stages, then the Node driver
#   ./run.sh clean    # remove everything generated
#
# Two stages, because they are two different jobs:
#
#   1. lib/  — the `dynamic` backend turns bank.h into
#              lib/bindings/dynamic/auto_dynamic.{h,cpp}. Needs clang-p2996.
#   2. meta/ — binds <rosetta/script.h> and compiles those tables in. Stock
#              compiler for the target; the generator still needs p2996.
#
# Stage 1 is skipped when its output already exists, so re-runs are quick.
set -euo pipefail
cd "$(dirname "$0")"

GEN=../../../bin/rosetta_gen

if [ "${1:-}" = "clean" ]; then
    (cd lib  && ../$GEN --clean manifest.json >/dev/null 2>&1 || true)
    (cd meta && ../$GEN --clean manifest.json >/dev/null 2>&1 || true)
    rm -rf lib/bindings lib/gen lib/generator meta/bindings meta/gen meta/generator
    echo "cleaned"
    exit 0
fi

# --- 1. your library -> metadata tables ---
if [ ! -f lib/bindings/dynamic/auto_dynamic.cpp ]; then
    echo ">> stage 1: bank.h -> auto_dynamic.{h,cpp}"
    (cd lib && ../$GEN --build manifest.json)
fi

# --- 2. the reflection API -> python / lua / node modules ---
echo ">> stage 2: <rosetta/script.h> -> rosetta_meta"
(cd meta && ../$GEN --build manifest.json)

# --- 3. drive it ---
echo
case "${1:-}" in
    lua)  (cd meta/bindings/lua && lua ../../drive.lua) ;;
    node) (cd meta && node drive.js) ;;
    *)    (cd meta && PYTHONPATH=bindings/python python3 drive.py) ;;
esac
