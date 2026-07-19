// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Shared helpers: file I/O and portable command running (see util.h).

#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

void write_file(const fs::path &p, const std::string &content) {
    // parent_path() is empty for a bare filename (e.g. "manifest.json" in the
    // cwd); create_directories("") throws, so only create when there's a dir.
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }
    std::ofstream(p) << content;
}

std::string read_file(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + p.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// write_file, but only when the content differs — re-emitting an unchanged
// manifest leaves the files' mtimes alone, so an already-configured
// out_dir/build recompiles nothing on the next `cmake --build`.
bool write_file_if_changed(const fs::path &p, const std::string &content) {
    {
        std::ifstream in(p, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            if (ss.str() == content) {
                return false;
            }
        }
    }
    write_file(p, content);
    return true;
}


// Quote one command-line argument. Double quotes are the one form both the
// POSIX shells and cmd.exe accept for embedded spaces.
std::string q(const std::string &s) {
    if (!s.empty() && s.find_first_of(" \t\"") == std::string::npos) {
        return s;
    }
    std::string out = "\"";
    for (char ch : s) {
        if (ch == '"') {
            out += '\\';
        }
        out += ch;
    }
    out += '"';
    return out;
}
std::string q(const fs::path &p) { return q(p.string()); }

// Run a command line (echoed), optionally from another working directory.
// std::system's return value is implementation-defined; we only rely on
// zero == success, which holds on POSIX and Windows alike.
int run_cmd(const std::string &cmd, const fs::path &cwd) {
    std::fprintf(stderr, "   $ %s\n", cmd.c_str());
    fs::path prev;
    if (!cwd.empty()) {
        prev = fs::current_path();
        fs::current_path(cwd);
    }
    const int rc = std::system(cmd.c_str());
    if (!prev.empty()) {
        fs::current_path(prev);
    }
    return rc;
}

// Is a tool on PATH? Probe with its --version, output discarded.
bool have_tool(const std::string &probe) {
#ifdef _WIN32
    const char *sink = " >nul 2>&1";
#else
    const char *sink = " >/dev/null 2>&1";
#endif
    return std::system((probe + " --version" + sink).c_str()) == 0;
}

// The generator binary lands next to the gen dir (single-config), or under a
// per-config subdir with a multi-config generator (MSVC).
fs::path find_generator(const fs::path &beside) {
#ifdef _WIN32
    const std::string exe = "generator.exe";
#else
    const std::string exe = "generator";
#endif
    for (const char *sub : {"", "Release", "Debug", "RelWithDebInfo"}) {
        const fs::path p = *sub ? beside / sub / exe : beside / exe;
        if (fs::exists(p)) {
            return p;
        }
    }
    return {};
}

