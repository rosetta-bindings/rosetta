// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// --build: the whole manifest pipeline in one command (emit + build + run the
// generator, then compile every declared backend).

#pragma once

// The --build option list, shared with the top-level --help text.
extern const char *kBuildOptions;

// Parse `rosetta_gen --build ...` arguments and run the pipeline.
int build_main(int argc, char **argv);
