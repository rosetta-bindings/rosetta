// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// Reflection-free Qt Widgets runtime for the "qt-expanded" backend.
//
// This is the stock-C++ counterpart of <rosetta/visitors/qt_visitor.h>: the
// same per-field editor logic (spinbox / slider / checkbox / combobox / line
// edit, with range / readonly / widget-hint / doc handling), but driven by
// *runtime* arguments instead of std::meta reflection. The expanded backend
// emits one `add_field<F>(...)` / `add_method(...)` call per member; F is
// deduced from the field value, so the generated code spells no field types and
// includes no <experimental/meta>. It builds with an ordinary C++17 compiler +
// Qt 6 — no clang-p2996, and (since nothing here declares Q_OBJECT) no moc on
// the generated side either.

#pragma once

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFormLayout>
#include <QRadioButton>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <climits>
#include <functional>
#include <string>
#include <type_traits>

namespace rosetta {
    namespace qtw {

        // Widget hint, mirroring rosetta::widget::* — passed as a plain int so
        // the runtime needs no rosetta headers.
        enum Hint {
            H_NONE = 0,
            H_SPIN,
            H_SLIDER,
            H_CHECKBOX,
            H_TEXTFIELD,
            H_COLOR,     // string "#rrggbb" -> swatch button + QColorDialog
            H_MULTILINE, // string -> QTextEdit
            H_RADIO,     // combobox choices -> QRadioButton group
            H_FILE,      // string path -> line edit + QFileDialog browse
        };

        // ---- value <-> QVariant (scalars + std::string), reflection-free ----

        template <typename F> inline QVariant to_variant(const F &v) {
            if constexpr (std::is_same_v<F, std::string>) {
                return QString::fromStdString(v);
            } else {
                return QVariant::fromValue(v);
            }
        }

        template <typename F> inline F from_variant(const QVariant &v) {
            if constexpr (std::is_same_v<F, std::string>) {
                return v.toString().toStdString();
            } else {
                return v.value<F>();
            }
        }

        inline void log_line(QTextEdit *log, const QString &msg, bool ok) {
            if (!log) {
                return;
            }
            const QString ts    = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
            const QString color = ok ? QStringLiteral("#4cb050") : QStringLiteral("#e57373");
            log->append(QStringLiteral("<span style=\"color:#555\">") + ts +
                        QStringLiteral("</span> <span style=\"color:") + color +
                        QStringLiteral("\">") + msg.toHtmlEscaped() + QStringLiteral("</span>"));
        }

        // ---- the inspector shell (mirrors qt_visitor's build_inspector) ----

        struct Shell {
            QWidget     *root = nullptr;
            QFormLayout *form = nullptr; // fields + methods share this, in walk order
            QTextEdit   *log  = nullptr; // nullptr when with_log == false
        };

        inline Shell make_shell(const QString &type_name, bool with_log) {
            Shell s;
            s.root = new QWidget();
            s.root->setWindowTitle(type_name + QStringLiteral(" — Qt Widgets inspector"));
            auto *vbox = new QVBoxLayout(s.root);

            auto *form_widget = new QWidget();
            form_widget->setMaximumWidth(560);
            s.form = new QFormLayout(form_widget);
            s.form->setLabelAlignment(Qt::AlignLeft);

            auto *form_row = new QHBoxLayout();
            form_row->setContentsMargins(0, 0, 0, 0);
            form_row->addWidget(form_widget);
            form_row->addStretch(1);
            vbox->addLayout(form_row);

            if (with_log) {
                s.log = new QTextEdit();
                s.log->setReadOnly(true);
                s.log->setStyleSheet(QStringLiteral("background:#0e0e0e; color:#e8e8e8;"));
                vbox->addWidget(s.log, 1);
            } else {
                vbox->addStretch(1);
            }
            return s;
        }

        // ---- one field row ----
        //
        // F is deduced from `initial`, so the caller writes no type. `commit` is
        // any callable taking the new value (the caller passes a generic lambda
        // `[&t](auto v){ t.field = v; }`); it's a separate template parameter so
        // F stays deduced solely from `initial`. Range / readonly / widget-hint /
        // combobox behaviour matches the reflective QtVisitor exactly.
        template <typename F, typename Commit>
        inline void add_field(QFormLayout *form, const QString &display, const QString &tip,
                              const F &initial, bool ro, bool has_rng, double rmin, double rmax,
                              const QStringList &choices, int hint, Commit commit, QTextEdit *log,
                              const QString &cname) {
            QWidget                                   *editor = nullptr;
            std::function<QVariant()>                  read_editor;
            std::function<void(std::function<void()>)> wire_commit;

            if constexpr (std::is_same_v<F, std::string>) {
                if (!choices.isEmpty() && hint == H_RADIO) {
                    // combobox choices as a radio-button group (widget::radio).
                    auto *wrap = new QWidget();
                    auto *whb  = new QHBoxLayout(wrap);
                    whb->setContentsMargins(0, 0, 0, 0);
                    auto *group = new QButtonGroup(wrap);
                    for (const QString &choice : choices) {
                        auto *rb = new QRadioButton(choice);
                        rb->setChecked(choice == QString::fromStdString(initial));
                        rb->setEnabled(!ro);
                        group->addButton(rb);
                        whb->addWidget(rb);
                    }
                    whb->addStretch(1);
                    editor      = wrap;
                    read_editor = [group] {
                        auto *b = group->checkedButton();
                        return QVariant(b ? b->text() : QString{});
                    };
                    wire_commit = [group](std::function<void()> c) {
                        QObject::connect(group, &QButtonGroup::buttonClicked, group,
                                         [c](QAbstractButton *) { c(); });
                    };
                } else if (!choices.isEmpty()) {
                    auto *box = new QComboBox();
                    box->addItems(choices);
                    const int idx = box->findText(QString::fromStdString(initial));
                    box->setCurrentIndex(idx < 0 ? 0 : idx);
                    box->setEnabled(!ro);
                    editor      = box;
                    read_editor = [box] { return QVariant(box->currentText()); };
                    wire_commit = [box](std::function<void()> c) {
                        QObject::connect(box, &QComboBox::activated, box, [c](int) { c(); });
                    };
                } else if (hint == H_COLOR) {
                    // "#rrggbb" string as a swatch button opening QColorDialog
                    // (widget::color). The button background previews the value.
                    auto *btn = new QPushButton(QString::fromStdString(initial));
                    auto  set_swatch = [btn](const QString &hex) {
                        btn->setText(hex);
                        btn->setStyleSheet(QStringLiteral("background:%1;").arg(hex));
                    };
                    set_swatch(QString::fromStdString(initial));
                    btn->setEnabled(!ro);
                    editor      = btn;
                    read_editor = [btn] { return QVariant(btn->text()); };
                    wire_commit = [btn, set_swatch](std::function<void()> c) {
                        QObject::connect(btn, &QPushButton::clicked, btn, [btn, set_swatch, c] {
                            const QColor picked = QColorDialog::getColor(
                                QColor(btn->text()), btn);
                            if (picked.isValid()) {
                                set_swatch(picked.name());
                                c();
                            }
                        });
                    };
                } else if (hint == H_MULTILINE) {
                    // Multi-line text area (widget::multiline); commits on
                    // focus-out via an event filter — QTextEdit has no
                    // editingFinished.
                    auto *te = new QTextEdit(QString::fromStdString(initial));
                    te->setMaximumHeight(96);
                    te->setReadOnly(ro);
                    editor      = te;
                    read_editor = [te] { return QVariant(te->toPlainText()); };
                    wire_commit = [te](std::function<void()> c) {
                        QObject::connect(te, &QTextEdit::textChanged, te, c);
                    };
                } else if (hint == H_FILE) {
                    // Path string: line edit + "…" browse button (widget::file).
                    auto *wrap = new QWidget();
                    auto *whb  = new QHBoxLayout(wrap);
                    whb->setContentsMargins(0, 0, 0, 0);
                    auto *le = new QLineEdit(QString::fromStdString(initial));
                    le->setReadOnly(ro);
                    auto *browse = new QPushButton(QStringLiteral("…"));
                    browse->setMaximumWidth(32);
                    browse->setEnabled(!ro);
                    whb->addWidget(le, 1);
                    whb->addWidget(browse);
                    editor      = wrap;
                    read_editor = [le] { return QVariant(le->text()); };
                    wire_commit = [le, browse](std::function<void()> c) {
                        QObject::connect(le, &QLineEdit::editingFinished, le, c);
                        QObject::connect(browse, &QPushButton::clicked, le, [le, c] {
                            const QString picked = QFileDialog::getOpenFileName(le);
                            if (!picked.isEmpty()) {
                                le->setText(picked);
                                c();
                            }
                        });
                    };
                } else {
                    auto *le = new QLineEdit(QString::fromStdString(initial));
                    le->setReadOnly(ro);
                    editor      = le;
                    read_editor = [le] { return QVariant(le->text()); };
                    wire_commit = [le](std::function<void()> c) {
                        QObject::connect(le, &QLineEdit::editingFinished, le, c);
                    };
                }
            } else if constexpr (std::is_same_v<F, bool>) {
                auto *cb = new QCheckBox();
                cb->setChecked(initial);
                cb->setEnabled(!ro);
                editor      = cb;
                read_editor = [cb] { return QVariant(cb->isChecked()); };
                wire_commit = [cb](std::function<void()> c) {
                    QObject::connect(cb, &QCheckBox::toggled, cb, [c](bool) { c(); });
                };
            } else if constexpr (std::is_integral_v<F>) {
                if (hint == H_SLIDER && has_rng) {
                    auto *wrap = new QWidget();
                    auto *whb  = new QHBoxLayout(wrap);
                    whb->setContentsMargins(0, 0, 0, 0);
                    auto *sl  = new QSlider(Qt::Horizontal);
                    auto *val = new QLabel();
                    sl->setRange(static_cast<int>(rmin), static_cast<int>(rmax));
                    sl->setValue(static_cast<int>(initial));
                    sl->setEnabled(!ro);
                    val->setMinimumWidth(40);
                    val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    val->setText(QString::number(sl->value()));
                    QObject::connect(sl, &QSlider::valueChanged, val,
                                     [val](int v) { val->setText(QString::number(v)); });
                    whb->addWidget(sl, 1);
                    whb->addWidget(val);
                    editor      = wrap;
                    read_editor = [sl] { return QVariant(sl->value()); };
                    wire_commit = [sl](std::function<void()> c) {
                        QObject::connect(sl, &QSlider::sliderReleased, sl, c);
                    };
                } else if (hint == H_CHECKBOX) {
                    auto *cbw = new QCheckBox();
                    cbw->setChecked(static_cast<bool>(initial));
                    cbw->setEnabled(!ro);
                    editor      = cbw;
                    read_editor = [cbw] { return QVariant(cbw->isChecked() ? 1 : 0); };
                    wire_commit = [cbw](std::function<void()> c) {
                        QObject::connect(cbw, &QCheckBox::toggled, cbw, [c](bool) { c(); });
                    };
                } else if (hint == H_TEXTFIELD) {
                    auto *le = new QLineEdit();
                    auto *vv = new QIntValidator(le);
                    if (has_rng) {
                        vv->setRange(static_cast<int>(rmin), static_cast<int>(rmax));
                    }
                    le->setValidator(vv);
                    le->setText(QString::number(static_cast<qlonglong>(initial)));
                    le->setReadOnly(ro);
                    editor      = le;
                    read_editor = [le] { return QVariant(le->text()); };
                    wire_commit = [le](std::function<void()> c) {
                        QObject::connect(le, &QLineEdit::editingFinished, le, c);
                    };
                } else {
                    auto *sb = new QSpinBox();
                    if (has_rng) {
                        sb->setRange(static_cast<int>(rmin), static_cast<int>(rmax));
                    } else {
                        sb->setRange(INT_MIN, INT_MAX);
                    }
                    sb->setValue(static_cast<int>(initial));
                    sb->setReadOnly(ro);
                    editor      = sb;
                    read_editor = [sb] { return QVariant(sb->value()); };
                    wire_commit = [sb](std::function<void()> c) {
                        QObject::connect(sb, &QSpinBox::valueChanged, sb, [c](int) { c(); });
                    };
                }
            } else if constexpr (std::is_floating_point_v<F>) {
                if (hint == H_TEXTFIELD) {
                    auto *le = new QLineEdit();
                    auto *vv = new QDoubleValidator(le);
                    if (has_rng) {
                        vv->setRange(rmin, rmax);
                    }
                    vv->setNotation(QDoubleValidator::ScientificNotation);
                    le->setValidator(vv);
                    le->setText(QString::number(static_cast<double>(initial), 'g', 12));
                    le->setReadOnly(ro);
                    editor      = le;
                    read_editor = [le] { return QVariant(le->text()); };
                    wire_commit = [le](std::function<void()> c) {
                        QObject::connect(le, &QLineEdit::editingFinished, le, c);
                    };
                } else {
                    auto *sb = new QDoubleSpinBox();
                    if (has_rng) {
                        sb->setRange(rmin, rmax);
                    } else {
                        sb->setRange(-1e12, 1e12);
                    }
                    sb->setDecimals(6);
                    sb->setValue(static_cast<double>(initial));
                    sb->setReadOnly(ro);
                    editor      = sb;
                    read_editor = [sb] { return QVariant(sb->value()); };
                    wire_commit = [sb](std::function<void()> c) {
                        QObject::connect(sb, &QDoubleSpinBox::valueChanged, sb,
                                         [c](double) { c(); });
                    };
                }
            } else {
                auto *le = new QLineEdit();
                le->setReadOnly(true);
                le->setPlaceholderText(QStringLiteral("(unsupported type)"));
                editor      = le;
                read_editor = [] { return QVariant{}; };
                wire_commit = [](std::function<void()>) {};
            }

            if (!ro) {
                QTextEdit *lp     = log;
                auto       do_set = [lp, read_editor, commit, has_rng, rmin, rmax, cname]() {
                    F val = from_variant<F>(read_editor());
                    if constexpr (std::is_arithmetic_v<F>) {
                        if (has_rng) {
                            double d = static_cast<double>(val);
                            if (d < rmin || d > rmax) {
                                log_line(lp, cname + QStringLiteral(" out of range"), false);
                                return;
                            }
                        }
                    }
                    commit(val);
                    log_line(lp, cname + QStringLiteral(" ← ") + to_variant<F>(val).toString(),
                             true);
                };
                wire_commit(do_set);
            }

            auto *lab = new QLabel(display);
            if (!tip.isEmpty()) {
                lab->setToolTip(tip);
            }
            form->addRow(lab, editor);
        }

        // A field whose type Qt can't edit (object / vector / enum): a disabled
        // placeholder, matching the reflective visitor's fallback.
        inline void add_unsupported(QFormLayout *form, const QString &display,
                                    const QString &tip) {
            auto *le = new QLineEdit();
            le->setReadOnly(true);
            le->setPlaceholderText(QStringLiteral("(unsupported type)"));
            auto *lab = new QLabel(display);
            if (!tip.isEmpty()) {
                lab->setToolTip(tip);
            }
            form->addRow(lab, le);
        }

        // One method row: a QLineEdit per parameter + a call button. `invoke`
        // receives the raw parameter strings and returns a result string to log
        // (the caller's lambda does the typed conversion + call).
        inline void add_method(QFormLayout *form, const QString &display, const QString &tip,
                               const QString &btn_label, const QStringList &param_names,
                               std::function<QString(const QStringList &)> invoke, QTextEdit *log,
                               const QString &cname) {
            auto *row  = new QWidget();
            auto *hbox = new QHBoxLayout(row);
            hbox->setContentsMargins(0, 0, 0, 0);

            auto *inputs = new QList<QLineEdit *>();
            for (const QString &pn : param_names) {
                auto *le = new QLineEdit();
                le->setPlaceholderText(pn);
                le->setMaximumWidth(140);
                hbox->addWidget(le);
                inputs->append(le);
            }
            const bool has_btn_label = !btn_label.isEmpty();
            if (!has_btn_label) {
                hbox->addStretch(1);
            }
            auto *call_btn = new QPushButton(has_btn_label ? btn_label : QStringLiteral("Call"));
            hbox->addWidget(call_btn);
            if (has_btn_label) {
                hbox->addStretch(1);
            }

            QTextEdit *lp = log;
            QObject::connect(call_btn, &QPushButton::clicked, row, [lp, inputs, invoke, cname] {
                QStringList args;
                for (auto *le : *inputs) {
                    args.append(le->text());
                }
                const QString r = invoke(args);
                log_line(lp, QStringLiteral("CALL ") + cname + QStringLiteral(" → ") + r, true);
            });

            if (has_btn_label) {
                form->addRow(row);
            } else {
                auto *lab = new QLabel(display);
                if (!tip.isEmpty()) {
                    lab->setToolTip(tip);
                }
                form->addRow(lab, row);
            }
        }

    } // namespace qtw
} // namespace rosetta
