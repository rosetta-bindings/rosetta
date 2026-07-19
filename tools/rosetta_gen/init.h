// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// --init: write a starter manifest — the commented example, or one
// pre-filled from a heuristic scan of a source directory.

#pragma once

#include <filesystem>

namespace fs = std::filesystem;

// Write a starter manifest to `path` — the commented example, or, when
// `scan_dir` is non-empty, one pre-filled from a scan of that directory
// (argv0 locates the rosetta checkout for the rosetta_include guess).
// Refuses to overwrite an existing file.
int init_manifest(const fs::path &path, const fs::path &scan_dir, const char *argv0);
