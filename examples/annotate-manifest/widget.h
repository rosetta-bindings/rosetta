// Copyright (c) fmaerten@gmail.com
// License: MIT

// A perfectly clean header: no annotations, no rosetta include. All of Widget's
// metadata lives in widget.ann.json, wired in by the manifest's "annotations"
// field. This file never mentions rosetta.
#pragma once

#include <string>

struct Widget {
    std::string title;
    int         count = 0;
    int         id    = 0;
    std::string mode;
};
