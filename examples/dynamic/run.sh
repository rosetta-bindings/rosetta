#!/usr/bin/env bash
# Build and run the dynamic object-model example, end-to-end from this directory:
#
#   ./run.sh            # terminal session (the canned script)
#   ./run.sh -i         # terminal session, interactive
#   ./run.sh inspect    # the registry walker the backend emits for free
#   ./run.sh viewer     # Qt window: 3D view + generated property panel + console
#   ./run.sh shot out.png
#                       # render the viewer's startup scene to a PNG and exit
#                       # (smoke test — needs no one at the keyboard)
#
# Stages (stage 1 is skipped if its output already exists, so re-runs are quick):
#   1. rosetta_gen + the generator driver -> bindings/dynamic/   (clang-p2996)
#   2. cmake build of the consumers + the generated auto_dynamic.cpp
#      (STOCK C++20 — scene.h is plain C++, all annotations live out of line in
#      *.ann.json; the Qt target is skipped automatically if Qt 6 is absent)
#   3. run it
set -euo pipefail
cd "$(dirname "$0")"

# --- 1. generate the metadata (clang-p2996) ---
if [ ! -f bindings/dynamic/auto_dynamic.cpp ]; then
    echo ">> generating from manifest.json"
    ../../bin/rosetta_gen manifest.json gen
    cmake -S gen -B gen/build
    cmake --build gen/build -j
    ./generator bindings
fi

# --- 2. build the consumers (stock compiler — nothing reflection-flavored left) ---
echo ">> building"
cmake -S . -B build
cmake --build build -j

# --- 3. run ---
case "${1:-}" in
    inspect)
        exec ./build/inspect
        ;;
    viewer)
        if [ ! -x ./build/viewer ]; then
            echo "The Qt viewer was not built — pass -DQT_DIR=/path/to/Qt/6.x/<platform>:" >&2
            echo "    cmake -S . -B build -DQT_DIR=\$HOME/Qt/6.8.3/macos" >&2
            exit 1
        fi
        shift
        exec ./build/viewer "$@"
        ;;
    shot)
        exec ./build/viewer --shot "${2:-viewer.png}"
        ;;
    *)
        exec ./build/demo "$@"
        ;;
esac
