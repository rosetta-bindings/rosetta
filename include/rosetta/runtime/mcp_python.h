// Copyright (c) fmaerten@gmail.com
// License: MIT

// The `run_python` tool for the MCP server: the agent sends a Python script
// that runs inside the server process, next to the live C++ objects.
//
// Same idea as `run_script` (<rosetta/runtime/mcp_lua.h>) — one tool call
// instead of dozens of round trips — with the trade-off flipped. Lua is small
// and sandboxable; Python is neither, but it brings the ecosystem: numpy over a
// mesh's positions, statistics, a quick fit. The script sees the same API:
//
//     m = obj("bunny")
//     r = new("Relaxer")                  # or classes.Relaxer()
//     r.iterations = 5                    # ranges / read-only enforced
//     passes = 0
//     while classes.Relaxer.minQuality(m) <= 0.1:
//         r.run(m)                        # methods: plain attribute calls
//         passes += r.lastPasses
//     {"passes": passes, "worst": r.worstQuality}   # last expression = result
//
// The result is the value of the script's last expression (as in a notebook
// cell), else a global named `result`. Objects become handles; long arrays are
// summarized; print() is captured. A failing call raises RuntimeError with the
// core's message, which the script can catch.
//
// NOT SANDBOXED. CPython cannot be meaningfully restricted from inside: the
// script can import os, open files, start processes. That is why this tool is
// opt-in everywhere — the generated `mcp` target builds it only with
// -DROSETTA_MCP_PYTHON=ON — and why its description tells the client so. Only
// enable it for an agent you would let run Python on that machine anyway.
//
// Limits. A wall-clock timeout raises TimeoutError inside the script (via
// Py_AddPendingCall, so it works without a SIGINT handler) — but only while
// Python bytecode runs; a long C++ call or a numpy kernel finishes first.
//
// Interpreter. Started on first use with pybind11::initialize_interpreter()
// and deliberately never finalized (finalizing at exit races static
// destructors). If the host already runs an interpreter — a Python app that
// embeds rosetta — that one is used. Calls must come from the thread that
// started the interpreter, which for the generated server and the Qt viewer is
// the main thread. Each call gets a fresh globals dict.
//
// Depends on pybind11 (embed) and a Python 3.10+ development install.
// Declarations only; the bodies live in inline/mcp_python.hxx.

#pragma once

#include <rosetta/runtime/mcp.h>

#include <cstddef>
#include <string>

namespace rosetta::mcp {

    struct PythonOptions {
        double      default_timeout_s = 30;
        double      max_timeout_s     = 600;
        std::size_t max_output        = 64u << 10; // print() capture, bytes
    };

    /** @brief Register the `run_python` tool on `server`. */
    void enable_python(Server &server, PythonOptions opts = {});

    /**
     * @brief Run one script against `server`'s registry and handle table.
     *
     * Returns {"result", "output", "elapsed_s"} like run_lua(); throws
     * dyn::Error carrying the Python traceback and the output so far.
     */
    json run_python(Server &server, const std::string &code, double timeout_s,
                    const PythonOptions &opts = {});

} // namespace rosetta::mcp

#include "inline/mcp_python.hxx"
