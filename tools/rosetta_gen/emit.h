// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Emission of the generator project tree (bindings.h + <generator_name>.cpp +
// CMakeLists.txt) — shared by the plain generate mode and --build.

#pragma once

#include "manifest.h"

// Emit the three files into out_dir (only those whose content changed).
// Returns whether any emitted file actually changed.
bool emit_generator_project(const Manifest &m, const fs::path &out_dir);
