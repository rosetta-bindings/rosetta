// Copyright (c) fmaerten@gmail.com
// License: MIT

// --api-diff: compare two api.json snapshots (written by every generation next
// to coverage.json) and report the breaking changes. See docs/API_DIFF.md.

#pragma once

// Parse `rosetta_gen --api-diff old new [options]` and print the report.
// Exit status: 0 = no breaking change, 1 = breaking change(s) (or, with
// --strict, warnings), 2 = usage or input error.
int api_diff_main(int argc, char **argv);
