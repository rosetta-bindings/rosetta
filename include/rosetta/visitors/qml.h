// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// QML backend: implements the rosetta::walk visitor concept.
//
// Provides:
//   - rosetta::ReflectedObject — backend-agnostic Q_OBJECT bridge exposed
//     to QML as a context property (declared in qml_reflected_object.h).
//   - rosetta::QmlVisitor<T> — emits one row of metadata per field/method
//     into a ReflectedObject, plus a typed lambda for each getter/setter
//     and method invoker.
//   - rosetta::bind_qml<T>(obj, target) — entry point: runs the walk and
//     leaves `obj` ready to be exposed to QML via a context property.
//
// The generic inspector markup that renders this metadata ships alongside
// at qml/Inspector.qml — add it to your qt_add_qml_module(QML_FILES ...)
// and load it with engine.loadFromModule(<your-uri>, "Inspector").
//
// Annotation behaviour mirrors the other backends:
//   readonly        -> setField returns an error
//   range{lo,hi}    -> setField rejects out-of-range numeric values
//   combobox{...}   -> setField rejects values outside the choice list; the
//                      choices are forwarded so the QML side can render a
//                      drop-down editor
//   widget::slider  -> forwarded as the `widget` hint so the QML side can
//                      swap a SpinBox for a Slider (orthogonal to validation)
//   widget::checkbox-> forwarded as the `widget` hint so the QML side can
//                      render an int 0/1 flag as a CheckBox
//   widget::textfield-> forwarded as the `widget` hint so the QML side
//                      can render a numeric field as a TextField with an
//                      IntValidator / DoubleValidator from range{lo, hi}
//   button{"label"} -> overrides the action button text on a method row
//                      (defaults to "Call" when absent)
//   doc             -> surfaced as `doc` on each field/method entry so the
//                      QML side can show it as a tooltip
//
// The implementation lives in inline/qml.hxx.

#pragma once

#include "qml_reflected_object.h"
#include <experimental/meta>
#include <rosetta/walk.h>

namespace rosetta {

    template <typename T> struct QmlVisitor {
        ReflectedObject *obj;
        T               *target;

        template <std::meta::info Fld, auto... Anns> void field(const char *name);
        template <std::meta::info Fn, auto... Anns> void method_instance(const char *name);
        template <std::meta::info Fn, auto... Anns> void method_static(const char *name);
    };

    template <typename T> void bind_qml(ReflectedObject *obj, T &target);

} // namespace rosetta

#include "inline/qml.hxx"
