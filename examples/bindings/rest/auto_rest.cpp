// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Auto-REST demo: walk-driven HTTP API for Person, served via cpp-httplib.
//
// Build flags: -freflection -freflection-latest -fannotation-attributes
// See CMakeLists.txt in this folder.

#include "../../person.h"
#include <rosetta/visitors/rest.h>
#include <cstdio>

int main(int argc, char **argv) {
    httplib::Server        server;

    // ---------------------------------------------------------------------------------
    // The rosetta part: bind a REST API to a simple in-memory store of Person objects.
    rosetta::Store<Person> persons;
    rosetta::bind_rest<Person>(server, "/Person", persons);
    // ---------------------------------------------------------------------------------

    // Serve index.html (the browser playground) from the same origin as the
    // API, so the page can hit /Person/* without any CORS dance. Defaults
    // to ".", so run from examples/rest/; override with an argv.
    const char *static_dir = (argc > 1) ? argv[1] : ".";
    if (!server.set_mount_point("/", static_dir)) {
        std::fprintf(stderr, "warning: could not mount static dir '%s'\n", static_dir);
    }

    const char *host = "127.0.0.1";
    int         port = 8080;
    std::printf("listening on http://%s:%d  (static from '%s')\n", host, port, static_dir);
    server.listen(host, port);
    return 0;
}
