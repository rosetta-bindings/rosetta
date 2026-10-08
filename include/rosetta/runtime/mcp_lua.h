// Copyright (c) fmaerten@gmail.com
// License: MIT

// The `run_script` tool for the MCP server: let the agent send a Lua script
// instead of a long sequence of tool calls.
//
// Every MCP tool call is a round trip through the model. "Relax until the
// worst triangle is above 0.1" as individual calls is run / measure / decide,
// repeated — dozens of round trips, each one paying for the model to re-read
// the conversation. As a script it is one call:
//
//     local m = obj("bunny")
//     local r = new("Relaxer")
//     r.iterations = 5
//     local passes = 0
//     while classes.Relaxer.minQuality(m) <= 0.1 do
//         r:run(m)
//         passes = passes + r.lastPasses
//     end
//     return {passes = passes, worst = r.worstQuality}
//
// The script sees the same objects the other tools do — `obj(handle)` fetches
// from the server's handle table, `keep(name, o)` publishes into it, and a
// returned object becomes a handle — and goes through the same core:
// overloads, ranges and read-only flags are enforced by rosetta::dyn, and a
// violation is a Lua error the script can pcall() or let propagate back to
// the agent with a traceback.
//
// Unlike the JSON tools, a script receives vectors WHOLE (as Lua tables): the
// point of running code next to the data is that bulk data never has to
// travel. Only what the script returns is summarized.
//
// Sandbox. A fresh Lua state per call, with base / table / string / math /
// utf8 / coroutine only — no io, os, package, debug, and no load / dofile /
// loadfile, so a script reaches the bound library and nothing else on the
// machine. A wall-clock limit (checked every 10k VM instructions) and a memory
// cap on the Lua heap stop runaway scripts; a single long C++ call cannot be
// interrupted, by design — it is the library's own code.
//
// Optional by construction: the core server (<rosetta/runtime/mcp.h>) does not
// depend on Lua. A host opts in with enable_lua(server), and the generated
// `mcp` target does so when CMake finds Lua 5.3+ (ROSETTA_MCP_LUA).
//
// Declarations only; the bodies live in inline/mcp_lua.hxx.

#pragma once

#include <rosetta/runtime/mcp.h>

#include <cstddef>
#include <string>

namespace rosetta::mcp {

    struct LuaOptions {
        double      default_timeout_s = 30;  // when the call gives none
        double      max_timeout_s     = 600; // the most a call may ask for
        std::size_t max_memory        = 512u << 20; // Lua heap cap, bytes
        std::size_t max_output        = 64u << 10;  // print() capture, bytes
    };

    /** @brief Register the `run_script` tool on `server`. */
    void enable_lua(Server &server, LuaOptions opts = {});

    /**
     * @brief Run one script against `server`'s registry and handle table.
     *
     * Returns {"result": <return value as JSON>, "output": <printed text>,
     * "elapsed_s": <seconds>}. Throws dyn::Error — carrying the Lua error,
     * its traceback and the output printed so far — when the script fails.
     */
    json run_lua(Server &server, const std::string &code, double timeout_s,
                 const LuaOptions &opts = {});

} // namespace rosetta::mcp

#include "inline/mcp_lua.hxx"
