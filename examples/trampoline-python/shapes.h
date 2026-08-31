// Copyright (c) fmaerten@gmail.com
// License: MIT

// A tiny polymorphic library — your existing, unmodified code. `Shape` has a
// pure virtual (`area`) and a virtual with a default (`describe`). rosetta
// reflects these, and because they are virtual the Python backend emits a
// pybind11 trampoline so a Python subclass can override them and have C++
// dispatch back into Python.
#pragma once

#include <string>

struct Shape {
    std::string label = "shape";

    // Pure virtual: a Python subclass MUST provide it.
    virtual double area() const = 0;

    // Virtual with a default: overridable, but falls back to this body.
    virtual std::string describe() const { return label; }

    virtual ~Shape() = default;
};
