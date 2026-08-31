// Copyright (c) fmaerten@gmail.com
// License: MIT

// Qt Widgets generation backend — *expanded* / reflection-free variant,
// registered under the "qt" target.
//
// The reflection-free counterpart of <rosetta/visitors/qt.h>: every
// field/method is expanded into an explicit call to the stock-Qt helpers in
// <rosetta/runtime/qt_widgets.h>, so the generated qt_inspector.cpp
// builds a property/method inspector window with an ordinary C++17 compiler +
// Qt 6 — no clang-p2996, no <experimental/meta>, and no moc on the generated
// side (nothing here declares Q_OBJECT).
//
// Output tree: qt/{qt_inspector.h, qt_inspector.cpp, main.cpp, CMakeLists.txt,
// README.md}. `build_<Class>_inspector(<Class>&, bool with_log)` is generated
// per class; main.cpp shows them in a QTabWidget.
//
// Implementation in inline/qt.hxx. Reuses py_lit() and
// find_annotation() from earlier in the generate pipeline.

#pragma once

namespace rosetta {
    namespace backend {
        using namespace gen_detail; // shared render / IR helpers

        struct Qt : Backend {
            void emit(const GenContext &c) const override;
        };

    } // namespace backend
} // namespace rosetta

#include "inline/qt.hxx"
