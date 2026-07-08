#!/usr/bin/env bash
# Build and run the Dear ImGui inspector for the Algo demo class, end-to-end
# from this directory:
#
#   ./run.sh                          # opens the inspector window
#   ROSETTA_IMGUI_FRAMES=5 ./run.sh   # smoke test: render 5 frames and exit
#
# Stages (each skipped if its output already exists, so re-runs are quick):
#   1. rosetta_gen + the generator driver -> bindings/imgui-expanded/  (clang-p2996)
#   2. cmake build of the inspector app   -> algo_imgui                 (STOCK C++20 —
#      Algo.h is plain C++, all annotations live out of line in Algo.ann.json;
#      Dear ImGui + GLFW are fetched automatically by cmake)
#   3. run it
set -euo pipefail
cd "$(dirname "$0")"

# --- 1. generate the inspector project (clang-p2996) ---
if [ ! -f bindings/imgui-expanded/main.cpp ]; then
    echo ">> generating from manifest.json"
    ../../bin/rosetta_gen manifest.json gen
    cmake -S gen -B gen/build
    cmake --build gen/build -j
    ./generator bindings
fi

# --- 2. build the app (stock compiler — nothing reflection-flavored left) ---
echo ">> building the inspector"
cmake -S bindings/imgui-expanded -B bindings/imgui-expanded/build
cmake --build bindings/imgui-expanded/build -j

# --- 3. run ---
exec ./bindings/imgui-expanded/build/algo_imgui
