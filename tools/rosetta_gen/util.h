// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Small shared helpers: file I/O and portable command running (std::system +
// std::filesystem only, so the same code drives sh on POSIX and cmd on
// Windows).

#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// --- files ----------------------------------------------------------------

// Write `content` to `p`, creating parent directories as needed.
void write_file(const fs::path &p, const std::string &content);

// Slurp a file; throws std::runtime_error when it cannot be opened.
std::string read_file(const fs::path &p);

// write_file, but only when the content differs — re-emitting unchanged
// output leaves the file's mtime alone, so an already-configured build dir
// recompiles nothing on the next `cmake --build`. Returns whether it wrote.
bool write_file_if_changed(const fs::path &p, const std::string &content);

// --- processes (--build / --clean) ----------------------------------------

// Quote one command-line argument. Double quotes are the one form both the
// POSIX shells and cmd.exe accept for embedded spaces.
std::string q(const std::string &s);
std::string q(const fs::path &p);

// Run a command line (echoed), optionally from another working directory.
// Zero return == success, on POSIX and Windows alike.
int run_cmd(const std::string &cmd, const fs::path &cwd = {});

// Is a tool on PATH? Probe with its --version, output discarded.
bool have_tool(const std::string &probe);

// The generator binary lands next to the gen dir (single-config), or under a
// per-config subdir with a multi-config generator (MSVC). Empty if absent.
fs::path find_generator(const fs::path &beside);
