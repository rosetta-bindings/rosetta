// Copyright (c) fmaerten@gmail.com
// License: MIT

// QML generation backend — *expanded* / reflection-free variant, registered
// under the "qml" target (Route A).
//
// The reflection-free counterpart of <rosetta/visitors/qml.h>: instead
// of walking T at the target's compile time, the generated qml_bindings.cpp
// fills the *generic* rosetta::ReflectedObject (a single moc'd Q_OBJECT shipped
// in <rosetta/visitors/qml_reflected_object.h>) with explicit metadata rows and
// typed getter/setter/invoker lambdas, then the stock qml/Inspector.qml renders
// it. So the generated code includes no <experimental/meta> and builds with an
// ordinary C++17 compiler + Qt 6 — moc only ever runs on the generic
// ReflectedObject and the QML module, never on per-type reflection.
//
// Output tree: qml/{qml_bindings.h, qml_bindings.cpp, main.cpp, CMakeLists.txt,
// README.md}. `bind_<Class>(ReflectedObject*, <Class>&)` is generated per
// class; main.cpp wires one class to the inspector.
//
// Implementation in inline/qml.hxx.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct Qml : Backend {
            void emit(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/qml.hxx"
