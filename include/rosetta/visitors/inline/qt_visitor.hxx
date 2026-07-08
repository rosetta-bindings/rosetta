// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

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
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>
#include <QVariant>
#include <QVariantList>
#include <QWidget>
#include <climits>
#include <experimental/meta>
#include <functional>
#include <rosetta/walk.h>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace rosetta {

    namespace widget_detail {

        // A type that can round-trip through QVariant in generated code. The
        // hard compile-breakers are raw C arrays (e.g. a method returning
        // `double (&)[3][3]` — can't declare `R r` nor copy into a QVariant) and
        // class types incomplete in this TU (no sizeof / copy). Everything else
        // goes through QVariant::fromValue / QVariant::value as before. A pointer
        // stays usable even to an incomplete type.
        // `sizeof(T*)` is always valid, so a pointer is judged by its pointee
        // (incomplete pointee ⇒ not bindable); a std::vector recurses into its
        // element type so a vector of incomplete-pointee is rejected too.
        template <class P>
        concept pointee_ok =
            std::is_void_v<std::remove_cv_t<std::remove_pointer_t<P>>> ||
            requires { sizeof(std::remove_pointer_t<P>); };

        template <class T> struct vector_elem {
            static constexpr bool is = false;
            using type               = void;
        };
        template <class E, class A> struct vector_elem<std::vector<E, A>> {
            static constexpr bool is = true;
            using type               = E;
        };

        template <class T> consteval bool is_bindable() {
            using U = std::remove_cvref_t<T>;
            if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
                return false;
            } else if constexpr (std::is_pointer_v<U>) {
                return pointee_ok<U>;
            } else if constexpr (vector_elem<U>::is) {
                return is_bindable<typename vector_elem<U>::type>();
            } else {
                return std::is_arithmetic_v<U> || std::is_enum_v<U> || std::is_void_v<U> ||
                       requires { sizeof(U); };
            }
        }

        template <class T>
        concept bindable_type = is_bindable<T>();

        template <std::meta::info Fn, std::size_t... Is>
        consteval bool params_bindable(std::index_sequence<Is...>) {
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            return (bindable_type<typename[:std::meta::type_of(params[Is]):]> && ...);
        }

        template <std::meta::info Fn> consteval bool bindable_fn() {
            constexpr auto arity = std::meta::parameters_of(Fn).size();
            return bindable_type<typename[:std::meta::return_type_of(Fn):]> &&
                   params_bindable<Fn>(std::make_index_sequence<arity>{});
        }

        template <typename F> QVariant to_variant(const F &v) {
            if constexpr (std::is_same_v<F, std::string>)
                return QString::fromStdString(v);
            else {
                return QVariant::fromValue(v);
            }
        }

        template <typename F> F from_variant(const QVariant &v) {
            if constexpr (std::is_same_v<F, std::string>)
                return v.toString().toStdString();
            else {
                return v.value<F>();
            }
        }

        template <std::meta::info Fn, typename T, std::size_t... Is>
        QVariant invoke_method_impl(T &self, const QVariantList &args, std::index_sequence<Is...>) {
            using R               = [:std::meta::return_type_of(Fn):];
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            if constexpr (!bindable_fn<Fn>()) {
                // Return or a parameter type can't cross the QVariant boundary
                // (raw array / incomplete type) — keep the method inert.
                return QVariant();
            } else if constexpr (std::is_void_v<R>) {
                (self.[:Fn:])(
                    from_variant<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                        args[Is])...);
                return {};
            } else {
                R r = (self.[:Fn:])(
                    from_variant<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                        args[Is])...);
                return to_variant<R>(r);
            }
        }

        template <std::meta::info Fn, std::size_t... Is>
        QVariant invoke_static_impl(const QVariantList &args, std::index_sequence<Is...>) {
            using R               = [:std::meta::return_type_of(Fn):];
            constexpr auto params = std::define_static_array(std::meta::parameters_of(Fn));
            if constexpr (!bindable_fn<Fn>()) {
                // Return or a parameter type can't cross the QVariant boundary
                // (raw array / incomplete type) — keep the method inert.
                return QVariant();
            } else if constexpr (std::is_void_v<R>) {
                ([:Fn:])(
                    from_variant<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                        args[Is])...);
                return {};
            } else {
                R r = ([:Fn:])(
                    from_variant<std::remove_cvref_t<typename[:std::meta::type_of(params[Is]):]>>(
                        args[Is])...);
                return to_variant<R>(r);
            }
        }

        // Free helper so the visitor doesn't need to outlive the walk —
        // handlers capture `log` directly instead of `this`. `log == nullptr`
        // disables logging (build_inspector with with_log = false).
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

    } // namespace widget_detail

    template <typename T>
    template <std::meta::info Fld, auto... Anns>
    inline void QtVisitor<T>::field(const char *name) {
        using F                       = [:std::meta::type_of(Fld):];
        constexpr auto dann           = ann::get_or<doc>(doc{""}, Anns...);
        constexpr bool ro             = ann::has<readonly>(Anns...);
        constexpr bool has_rng        = ann::has<range>(Anns...);
        constexpr auto rng            = ann::get_or<range>(range{0, 0}, Anns...);
        constexpr bool has_cb         = ann::has<combobox>(Anns...);
        constexpr auto cb             = ann::get_or<combobox>(combobox{}, Anns...);
        constexpr bool want_slider    = ann::has<widget::slider_tag>(Anns...);
        constexpr bool want_checkbox  = ann::has<widget::checkbox_tag>(Anns...);
        constexpr bool want_textfield = ann::has<widget::textfield_tag>(Anns...);
        constexpr bool want_color     = ann::has<widget::color_tag>(Anns...);
        constexpr bool want_multiline = ann::has<widget::multiline_tag>(Anns...);
        constexpr bool want_radio     = ann::has<widget::radio_tag>(Anns...);
        constexpr bool want_file      = ann::has<widget::file_tag>(Anns...);
        constexpr auto lbl            = ann::get_or<label>(label{""}, Anns...);
        const QString  qname          = QString::fromUtf8(name);
        const QString  qdisplay       = (lbl.text[0] != '\0') ? QString::fromUtf8(lbl.text) : qname;
        T             *tgt            = target;

        // -------- editor widget + commit-signal wiring --------
        //
        // Each branch builds the editor and stores a `wire_commit`
        // lambda that knows which Qt signal of *this* editor means
        // "user finished a change" (returnPressed/valueChanged/
        // toggled/activated/sliderReleased). After the branches we
        // build the actual commit lambda and call wire_commit once.
        QWidget                                   *editor = nullptr;
        std::function<QVariant()>                  read_editor;
        std::function<void(std::function<void()>)> wire_commit;

        if constexpr (has_cb && want_radio && std::is_same_v<F, std::string>) {
            // combobox choices as a radio-button group (widget::radio).
            auto *wrap = new QWidget();
            auto *whb  = new QHBoxLayout(wrap);
            whb->setContentsMargins(0, 0, 0, 0);
            auto         *group   = new QButtonGroup(wrap);
            const QString current = QString::fromStdString(tgt->[:Fld:]);
            for (std::size_t i = 0; i < cb.count; ++i) {
                auto *rb = new QRadioButton(QString::fromUtf8(cb.choices[i]));
                rb->setChecked(rb->text() == current);
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
            wire_commit = [group](std::function<void()> commit) {
                QObject::connect(group, &QButtonGroup::buttonClicked, group,
                                 [commit](QAbstractButton *) { commit(); });
            };
        } else if constexpr (has_cb && std::is_same_v<F, std::string>) {
            auto *box = new QComboBox();
            for (std::size_t i = 0; i < cb.count; ++i) {
                box->addItem(QString::fromUtf8(cb.choices[i]));
            }
            const QString current = QString::fromStdString(tgt->[:Fld:]);
            const int     idx     = box->findText(current);
            box->setCurrentIndex(idx < 0 ? 0 : idx);
            box->setEnabled(!ro);
            editor      = box;
            read_editor = [box] { return QVariant(box->currentText()); };
            wire_commit = [box](std::function<void()> commit) {
                QObject::connect(box, &QComboBox::activated, box, [commit](int) { commit(); });
            };
        } else if constexpr (want_color && std::is_same_v<F, std::string>) {
            // "#rrggbb" string as a swatch button opening QColorDialog.
            auto *btn        = new QPushButton(QString::fromStdString(tgt->[:Fld:]));
            auto  set_swatch = [btn](const QString &hex) {
                btn->setText(hex);
                btn->setStyleSheet(QStringLiteral("background:%1;").arg(hex));
            };
            set_swatch(QString::fromStdString(tgt->[:Fld:]));
            btn->setEnabled(!ro);
            editor      = btn;
            read_editor = [btn] { return QVariant(btn->text()); };
            wire_commit = [btn, set_swatch](std::function<void()> commit) {
                QObject::connect(btn, &QPushButton::clicked, btn, [btn, set_swatch, commit] {
                    const QColor picked = QColorDialog::getColor(QColor(btn->text()), btn);
                    if (picked.isValid()) {
                        set_swatch(picked.name());
                        commit();
                    }
                });
            };
        } else if constexpr (want_multiline && std::is_same_v<F, std::string>) {
            // Multi-line text area (widget::multiline).
            auto *te = new QTextEdit(QString::fromStdString(tgt->[:Fld:]));
            te->setMaximumHeight(96);
            te->setReadOnly(ro);
            editor      = te;
            read_editor = [te] { return QVariant(te->toPlainText()); };
            wire_commit = [te](std::function<void()> commit) {
                QObject::connect(te, &QTextEdit::textChanged, te, commit);
            };
        } else if constexpr (want_file && std::is_same_v<F, std::string>) {
            // Path string: line edit + "…" browse button (widget::file).
            auto *wrap = new QWidget();
            auto *whb  = new QHBoxLayout(wrap);
            whb->setContentsMargins(0, 0, 0, 0);
            auto *le = new QLineEdit(QString::fromStdString(tgt->[:Fld:]));
            le->setReadOnly(ro);
            auto *browse = new QPushButton(QStringLiteral("…"));
            browse->setMaximumWidth(32);
            browse->setEnabled(!ro);
            whb->addWidget(le, 1);
            whb->addWidget(browse);
            editor      = wrap;
            read_editor = [le] { return QVariant(le->text()); };
            wire_commit = [le, browse](std::function<void()> commit) {
                QObject::connect(le, &QLineEdit::editingFinished, le, commit);
                QObject::connect(browse, &QPushButton::clicked, le, [le, commit] {
                    const QString picked = QFileDialog::getOpenFileName(le);
                    if (!picked.isEmpty()) {
                        le->setText(picked);
                        commit();
                    }
                });
            };
        } else if constexpr (want_slider && std::is_integral_v<F> && has_rng) {
            auto *wrap = new QWidget();
            auto *whb  = new QHBoxLayout(wrap);
            whb->setContentsMargins(0, 0, 0, 0);
            auto *sl  = new QSlider(Qt::Horizontal);
            auto *val = new QLabel();
            sl->setRange(static_cast<int>(rng.min), static_cast<int>(rng.max));
            sl->setValue(static_cast<int>(tgt->[:Fld:]));
            sl->setEnabled(!ro);
            val->setMinimumWidth(40);
            val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            val->setText(QString::number(sl->value()));
            QObject::connect(sl, &QSlider::valueChanged, val,
                             [val](int v) { val->setText(QString::number(v)); });
            whb->addWidget(sl, /*stretch*/ 1);
            whb->addWidget(val);
            editor      = wrap;
            read_editor = [sl] { return QVariant(sl->value()); };
            wire_commit = [sl](std::function<void()> commit) {
                QObject::connect(sl, &QSlider::sliderReleased, sl, commit);
            };
        } else if constexpr (want_checkbox && std::is_arithmetic_v<F>) {
            auto *cbw = new QCheckBox();
            cbw->setChecked(static_cast<bool>(tgt->[:Fld:]));
            cbw->setEnabled(!ro);
            editor      = cbw;
            read_editor = [cbw] { return QVariant(cbw->isChecked() ? 1 : 0); };
            wire_commit = [cbw](std::function<void()> commit) {
                QObject::connect(cbw, &QCheckBox::toggled, cbw, [commit](bool) { commit(); });
            };
        } else if constexpr (want_textfield && std::is_arithmetic_v<F>) {
            auto *le = new QLineEdit();
            if constexpr (std::is_integral_v<F>) {
                auto *v = new QIntValidator(le);
                if constexpr (has_rng)
                    v->setRange(static_cast<int>(rng.min), static_cast<int>(rng.max));
                le->setValidator(v);
                le->setText(QString::number(static_cast<qlonglong>(tgt->[:Fld:])));
            } else {
                auto *v = new QDoubleValidator(le);
                if constexpr (has_rng)
                    v->setRange(rng.min, rng.max);
                v->setNotation(QDoubleValidator::ScientificNotation);
                le->setValidator(v);
                le->setText(QString::number(static_cast<double>(tgt->[:Fld:]), 'g', 12));
            }
            le->setReadOnly(ro);
            editor = le;
            // QVariant(QString) -> from_variant<int/double> uses Qt's
            // built-in string-to-numeric conversion.
            read_editor = [le] { return QVariant(le->text()); };
            wire_commit = [le](std::function<void()> commit) {
                QObject::connect(le, &QLineEdit::editingFinished, le, commit);
            };
        } else if constexpr (std::is_same_v<F, std::string>) {
            auto *le = new QLineEdit(QString::fromStdString(tgt->[:Fld:]));
            le->setReadOnly(ro);
            editor      = le;
            read_editor = [le] { return QVariant(le->text()); };
            wire_commit = [le](std::function<void()> commit) {
                QObject::connect(le, &QLineEdit::editingFinished, le, commit);
            };
        } else if constexpr (std::is_same_v<F, bool>) {
            auto *cb = new QCheckBox();
            cb->setChecked(tgt->[:Fld:]);
            cb->setEnabled(!ro);
            editor      = cb;
            read_editor = [cb] { return QVariant(cb->isChecked()); };
            wire_commit = [cb](std::function<void()> commit) {
                QObject::connect(cb, &QCheckBox::toggled, cb, [commit](bool) { commit(); });
            };
        } else if constexpr (std::is_integral_v<F>) {
            auto *sb = new QSpinBox();
            if constexpr (has_rng)
                sb->setRange(static_cast<int>(rng.min), static_cast<int>(rng.max));
            else {
                sb->setRange(INT_MIN, INT_MAX);
            }
            sb->setValue(static_cast<int>(tgt->[:Fld:]));
            sb->setReadOnly(ro);
            editor      = sb;
            read_editor = [sb] { return QVariant(sb->value()); };
            // valueChanged fires on arrow click + Enter; connection is
            // wired *after* setValue above, so the initial assignment
            // does not commit.
            wire_commit = [sb](std::function<void()> commit) {
                QObject::connect(sb, &QSpinBox::valueChanged, sb, [commit](int) { commit(); });
            };
        } else if constexpr (std::is_floating_point_v<F>) {
            auto *sb = new QDoubleSpinBox();
            if constexpr (has_rng)
                sb->setRange(rng.min, rng.max);
            else {
                sb->setRange(-1e12, 1e12);
            }
            sb->setDecimals(6);
            sb->setValue(static_cast<double>(tgt->[:Fld:]));
            sb->setReadOnly(ro);
            editor      = sb;
            read_editor = [sb] { return QVariant(sb->value()); };
            wire_commit = [sb](std::function<void()> commit) {
                QObject::connect(sb, &QDoubleSpinBox::valueChanged, sb,
                                 [commit](double) { commit(); });
            };
        } else {
            auto *le = new QLineEdit();
            le->setReadOnly(true);
            le->setPlaceholderText(QStringLiteral("(unsupported type)"));
            editor      = le;
            read_editor = [] { return QVariant{}; };
            wire_commit = [](std::function<void()>) {};
        }

        // -------- commit on edit (no GET / PUT buttons) --------
        // `bindable_type<F>` gates the setter: a raw-array / incomplete field
        // type can't round-trip through QVariant (the editor above already
        // falls back to a read-only "(unsupported type)" line edit), so wiring
        // a commit lambda would be a hard compile error.
        if constexpr (!ro && widget_detail::bindable_type<F>) {
            QTextEdit *log_ptr = log;
            auto       commit  = [log_ptr, tgt, read_editor, qname]() {
                F val = widget_detail::from_variant<F>(read_editor());
                if constexpr (has_rng && std::is_arithmetic_v<F>) {
                    double d = static_cast<double>(val);
                    if (d < rng.min || d > rng.max) {
                        widget_detail::log_line(log_ptr, qname + QStringLiteral(" out of range"),
                                                false);
                        return;
                    }
                }
                tgt->[:Fld:] = val;
                widget_detail::log_line(log_ptr,
                                        qname + QStringLiteral(" ← ") +
                                            widget_detail::to_variant<F>(val).toString(),
                                        true);
            };
            wire_commit(commit);
        }

        // -------- label with [range] / [ro] hints + doc tooltip --------
        QString label_text = qdisplay;
        // if constexpr (has_rng)
        //     label_text += QStringLiteral(" [%1..%2]")
        //                       .arg(rng.min)
        //                       .arg(rng.max);
        // if constexpr (ro)
        //     label_text += QStringLiteral(" [ro]");
        auto *label = new QLabel(label_text);
        if (dann.text[0] != '\0') {
            label->setToolTip(QString::fromUtf8(dann.text));
        }

        fields_layout->addRow(label, editor);
    }

    template <typename T>
    template <std::meta::info Fn, auto... Anns>
    inline void QtVisitor<T>::method_instance(const char *name) {
        constexpr auto dann     = ann::get_or<doc>(doc{""}, Anns...);
        constexpr auto btn      = ann::get_or<button>(button{}, Anns...);
        constexpr auto lbl      = ann::get_or<label>(label{""}, Anns...);
        constexpr auto arity    = std::define_static_array(std::meta::parameters_of(Fn)).size();
        const QString  qname    = QString::fromUtf8(name);
        const QString  qdisplay = (lbl.text[0] != '\0') ? QString::fromUtf8(lbl.text) : qname;
        T             *tgt      = target;

        auto *row  = new QWidget();
        auto *hbox = new QHBoxLayout(row);
        hbox->setContentsMargins(0, 0, 0, 0);

        std::vector<QLineEdit *> inputs;
        template for (constexpr auto p : std::define_static_array(std::meta::parameters_of(Fn))) {
            auto *le = new QLineEdit();
            le->setPlaceholderText(QString::fromUtf8(std::meta::identifier_of(p)));
            le->setMaximumWidth(140);
            hbox->addWidget(le);
            inputs.push_back(le);
        }
        const bool has_btn_label = (btn.label[0] != '\0');
        if (!has_btn_label) {
            hbox->addStretch(1);
        }

        QTextEdit    *log_ptr = log;
        const QString btn_text =
            has_btn_label ? QString::fromUtf8(btn.label) : QStringLiteral("Call");
        auto *call_btn = new QPushButton(btn_text);
        hbox->addWidget(call_btn);
        if (has_btn_label) {
            hbox->addStretch(1);
        }
        QObject::connect(call_btn, &QPushButton::clicked, row, [log_ptr, tgt, inputs, qname] {
            QVariantList args;
            for (auto *le : inputs) {
                args.append(QVariant(le->text()));
            }
            QVariant result = widget_detail::invoke_method_impl<Fn>(
                *tgt, args, std::make_index_sequence<arity>{});
            widget_detail::log_line(
                log_ptr,
                QStringLiteral("CALL ") + qname + QStringLiteral(" → ") + result.toString(), true);
        });

        if (btn.label[0] != '\0') {
            methods_layout->addRow(row);
        } else {
            auto *label = new QLabel(qdisplay);
            if (dann.text[0] != '\0') {
                label->setToolTip(QString::fromUtf8(dann.text));
            }
            methods_layout->addRow(label, row);
        }
    }

    template <typename T>
    template <std::meta::info Fn, auto... Anns>
    inline void QtVisitor<T>::method_static(const char *name) {
        constexpr auto dann     = ann::get_or<doc>(doc{""}, Anns...);
        constexpr auto btn      = ann::get_or<button>(button{}, Anns...);
        constexpr auto lbl      = ann::get_or<label>(label{""}, Anns...);
        constexpr auto arity    = std::define_static_array(std::meta::parameters_of(Fn)).size();
        const QString  qname    = QString::fromUtf8(name);
        const QString  qdisplay = (lbl.text[0] != '\0') ? QString::fromUtf8(lbl.text) : qname;

        auto *row  = new QWidget();
        auto *hbox = new QHBoxLayout(row);
        hbox->setContentsMargins(0, 0, 0, 0);

        std::vector<QLineEdit *> inputs;
        template for (constexpr auto p : std::define_static_array(std::meta::parameters_of(Fn))) {
            auto *le = new QLineEdit();
            le->setPlaceholderText(QString::fromUtf8(std::meta::identifier_of(p)));
            le->setMaximumWidth(140);
            hbox->addWidget(le);
            inputs.push_back(le);
        }
        const bool has_btn_label = (btn.label[0] != '\0');
        if (!has_btn_label) {
            hbox->addStretch(1);
        }

        QTextEdit    *log_ptr = log;
        const QString btn_text =
            has_btn_label ? QString::fromUtf8(btn.label) : QStringLiteral("Call");
        auto *call_btn = new QPushButton(btn_text);
        hbox->addWidget(call_btn);
        if (has_btn_label) {
            hbox->addStretch(1);
        }
        QObject::connect(call_btn, &QPushButton::clicked, row, [log_ptr, inputs, qname] {
            QVariantList args;
            for (auto *le : inputs) {
                args.append(QVariant(le->text()));
            }
            QVariant result =
                widget_detail::invoke_static_impl<Fn>(args, std::make_index_sequence<arity>{});
            widget_detail::log_line(
                log_ptr,
                QStringLiteral("CALL ") + qname + QStringLiteral(" → ") + result.toString(), true);
        });

        if (btn.label[0] != '\0') {
            methods_layout->addRow(row);
        } else {
            auto *label = new QLabel(qdisplay + QStringLiteral("(") + QString::number(arity) +
                                     QStringLiteral(") [static]"));
            if (dann.text[0] != '\0') {
                label->setToolTip(QString::fromUtf8(dann.text));
            }
            methods_layout->addRow(label, row);
        }
    }

    template <typename T>
    inline QWidget *build_inspector(T &target, const QString &type_name, bool with_log) {
        auto *root = new QWidget();
        root->setWindowTitle(type_name + QStringLiteral(" — Qt Widgets inspector"));

        auto *vbox = new QVBoxLayout(root);

        // auto *heading = new QLabel(type_name +
        //                            QStringLiteral(" — generated from one "
        //                                           "C++26 reflection walk"));
        // heading->setStyleSheet(QStringLiteral("color:#888;"));
        // vbox->addWidget(heading);

        // One panel for fields + methods. The form widget is capped in
        // width and wrapped in an HBox with a stretch on the right, so
        // the pack stays glued to the left edge as the window resizes.
        auto *form_widget = new QWidget();
        form_widget->setMaximumWidth(560);
        auto *form_layout = new QFormLayout(form_widget);
        form_layout->setLabelAlignment(Qt::AlignLeft);

        auto *form_row = new QHBoxLayout();
        form_row->setContentsMargins(0, 0, 0, 0);
        form_row->addWidget(form_widget);
        form_row->addStretch(1);
        vbox->addLayout(form_row);

        QTextEdit *log = nullptr;
        if (with_log) {
            log = new QTextEdit();
            log->setReadOnly(true);
            log->setFontFamily(QStringLiteral("Menlo"));
            log->setStyleSheet(QStringLiteral("background:#0e0e0e; color:#e8e8e8;"));
            vbox->addWidget(log, /*stretch*/ 1);
        } else {
            // Without a log to soak up vertical space, pin the form to
            // the top and leave the bottom empty.
            vbox->addStretch(1);
        }

        // Fields and methods share the same form_layout — they appear in
        // walk order (fields first, then methods). `log` may be nullptr;
        // log_line() short-circuits in that case.
        QtVisitor<T> v{form_layout, form_layout, log, &target};
        walk<T>(v);
        return root;
    }

} // namespace rosetta
