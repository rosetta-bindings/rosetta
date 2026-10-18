#!/usr/bin/env bash
# One documented C++ class, four targets — see README.md.
#
#   ./run.sh            # generate (once), build, run the Python and Lua scripts
#   ./run.sh python     # nanobind module + example_nanobind.py
#   ./run.sh lua        # sol2 module    + example_lua.lua
#   ./run.sh mcp        # MCP server: a scripted session, then how to connect Claude
#
# Stage 1 needs clang-p2996; everything after it builds with a stock compiler,
# because pendulum.h is plain C++ and its metadata lives in Pendulum.ann.json.
set -euo pipefail
cd "$(dirname "$0")"

# --- 1. generate (clang-p2996) ---
if [ ! -f bindings/mcp/mcp_server.cpp ]; then
    echo ">> generating from manifest.json"
    ../../bin/rosetta_gen manifest.json gen
    cmake -S gen -B gen/build
    cmake --build gen/build -j
    ./generator bindings
fi

build() { # $1 = target
    cmake -S "bindings/$1" -B "bindings/$1/build" >/dev/null
    cmake --build "bindings/$1/build" -j
}

run_python() { build nanobind; echo; python3 example_nanobind.py; }
run_lua()    { build lua;      echo; lua example_lua.lua; }

run_mcp() {
    build mcp
    local server="$PWD/bindings/mcp/build/pendulum_mcp"
    echo
    echo ">> a scripted MCP session (what an agent's tool calls look like)"
    "$server" < mcp_session.jsonl | python3 -c '
import json, sys
for line in sys.stdin:
    m = json.loads(line)
    r = m.get("result", {})
    if "content" in r:
        body = r["content"][0]["text"] if r["isError"] else json.dumps(r["structuredContent"])
        print(("  ERROR " if r["isError"] else "  ") + body.replace("\n", " ")[:300])'
    echo ">> connect Claude Code to it:"
    echo "    claude mcp add pendulum -- \"$server\""
    echo "   then ask, for instance:"
    echo "    \"What rod length gives a 2 second period? Verify it by simulation.\""
}

case "${1:-}" in
    python) run_python ;;
    lua)    run_lua ;;
    mcp)    run_mcp ;;
    *)      run_python; echo; run_lua ;;
esac
