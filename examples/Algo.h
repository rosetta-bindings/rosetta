// Copyright (c) fmaerten@gmail.com
// License: MIT

// Shared demo type — annotated with rosetta::doc / range / readonly.
// Both examples/python/auto_pybind.cpp and examples/node/auto_napi.cpp
// bind this same struct to demonstrate that the visitor pattern is
// language-agnostic.

#pragma once

#include <rosetta/annotations.h>
#include <string>
#include <utility>

class Algo {
public:

    [[ = rosetta::doc{"Tolerence of the solver"}, = rosetta::range{1e-10, 1e-6}, = rosetta::widget::textfield, = rosetta::label{"EPS"} ]]
    double eps{1e-7};

    [[ = rosetta::doc{"Set the max iterations"}, = rosetta::range{0, 200}, = rosetta::widget::slider, = rosetta::label{"Max iter"} ]]
    int maxIter{100};

    [[ = rosetta::doc{"Tell if the solver must be iterative"}, = rosetta::widget::checkbox, = rosetta::label{"Iterative solver"} ]]
    bool iterative{true};

    [[ = rosetta::doc{"Solver name"}, = rosetta::combobox{{"Seidel", "Jacobi", "gmres", "cgnr"}}, = rosetta::label{"Solver name"} ]]
    std::string solverName{"Seidel"};

    [[ = rosetta::doc{"Run the solver and return the convergence"}, =rosetta::button{"Run"} ]]
    double run() {return 1e-8;}

    [[ = rosetta::doc{"Reset the solver"}, =rosetta::button{"Reset"} ]]
    void reset() {}
};
