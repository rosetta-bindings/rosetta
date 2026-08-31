// Copyright (c) fmaerten@gmail.com
// License: MIT

// Qt Widgets backend: implements the rosetta::walk visitor concept.
//
// Provides:
//   - rosetta::QtVisitor<T> — emits one editor per field into a
//     QFormLayout; each editor commits to the target struct directly on
//     its natural signal (QLineEdit::editingFinished,
//     QSpinBox::valueChanged, QCheckBox::toggled, QComboBox::activated,
//     QSlider::sliderReleased). No GET / PUT buttons.
//   - rosetta::build_inspector<T>(target, typeName) — entry point: builds
//     the window, runs the walk, returns a QWidget you can show().
//
// Annotation behaviour mirrors the qml / python / rest backends:
//   readonly         -> editor disabled, "[ro]" suffix on the field label
//   range{lo,hi}     -> bounds set on spinbox/slider; commit also re-checks
//   combobox{{...}}  -> editor becomes a QComboBox of the allowed choices
//   widget::slider   -> int + range renders as QSlider + value label
//                       instead of QSpinBox
//   widget::checkbox -> arithmetic field renders as QCheckBox (0/1)
//   widget::textfield-> numeric field renders as QLineEdit with
//                       QIntValidator / QDoubleValidator; bounds come
//                       from a separate range{lo, hi}
//   button{"label"}  -> overrides the action button text on a method row
//                       (defaults to "Call" when absent)
//   doc              -> tooltip on the field/method label

#pragma once

#include <QString>

class QWidget;
class QFormLayout;
class QTextEdit;

namespace rosetta {

    template <typename T> struct QtVisitor {
        QFormLayout *fields_layout;
        QFormLayout *methods_layout;
        QTextEdit   *log;
        T           *target;

        template <std::meta::info Fld, auto... Anns> void field(const char *name);
        template <std::meta::info Fn, auto... Anns> void method_instance(const char *name);
        template <std::meta::info Fn, auto... Anns> void method_static(const char *name);
    };

    template <typename T>
    QWidget *build_inspector(T &target, const QString &type_name, bool with_log = false);

} // namespace rosetta

#include "inline/qt.hxx"