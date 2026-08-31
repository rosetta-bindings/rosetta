// Copyright (c) fmaerten@gmail.com
// License: MIT

// A tiny polymorphic library — your existing, unmodified code. Because `area`
// and `describe` are virtual, the Node backend emits an N-API trampoline so a
// JavaScript subclass can override them and have C++ dispatch back into JS.
//
// `scaled_area` is a *non-virtual* method that internally calls the virtual
// `area()`; it demonstrates C++ -> JS dispatch (calling it on a JS subclass
// runs the JS override).
#pragma once

#include <string>

struct Shape {
    std::string label = "shape";

    virtual double      area() const = 0;                  // pure virtual
    virtual std::string describe() const { return label; } // virtual + default

    double scaled_area(double k) const { return k * area(); }

    virtual ~Shape() = default;
};
