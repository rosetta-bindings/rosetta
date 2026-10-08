// Copyright (c) fmaerten@gmail.com
// License: MIT

// Keeping an MCP server's handle table and the interpreter's variables the
// same set of objects — shared by the Claude panel and the HTTP MCP host.
//
// Before a tool call the server sees exactly what the scene holds, under the
// names the console and the object list use; after it, the scene adopts
// whatever the call created, kept or released. Both sides hold dyn::Object
// handles that share ownership, so swapping one table for the other never
// destroys an object the other side still holds.

#pragma once

#include <rosetta/runtime/mcp.h>

#include "../interp.h"

namespace scenesync {

    inline void publish(const dynui::Interp &interp, rosetta::mcp::Server &server) {
        server.clear_objects();
        for (const auto &[n, o] : interp.vars) {
            server.put(n, o);
        }
    }

    inline void adopt(dynui::Interp &interp, const rosetta::mcp::Server &server) {
        interp.vars.clear();
        for (const auto &[h, o] : server.objects()) {
            interp.vars[h] = o;
        }
    }

} // namespace scenesync
