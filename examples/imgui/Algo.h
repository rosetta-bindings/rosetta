// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Stock C++ — no annotations, no rosetta include (compare with ../Algo.h,
// the inline-annotated original). Every doc / range / combobox lives out of
// line in Algo.ann.json, wired in by the manifest's "annotations" field, so
// the generated ImGui inspector builds with a stock C++20 compiler.

#pragma once

#include <string>

class Algo {
public:
    double      eps{1e-7};
    int         maxIter{100};
    bool        iterative{true};
    std::string solverName{"Seidel"};
    std::string precond{"none"};        // radio group (see Algo.ann.json)
    std::string plotColor{"#4488ee"};   // color picker
    std::string meshFile{};             // file path + browse button
    std::string notes{"Converges quadratically\nnear the solution."}; // multi-line

    double run() { return 1e-8; }
    void   reset() {}
};
