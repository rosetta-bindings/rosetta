// Copyright (c) fmaerten@gmail.com
// License: MIT

// A property editor for a class this file has never heard of.
//
// Every widget below is chosen by asking the metadata, never by naming a type:
//
//   rosetta::label      -> the row's caption
//   rosetta::doc        -> the tooltip
//   rosetta::range      -> the slider / spinbox bounds (and the core rejects
//                          an out-of-range write anyway, so this is a hint,
//                          not the enforcement)
//   rosetta::readonly   -> a disabled row
//   rosetta::combobox   -> a QComboBox of string choices
//   an enum's TypeDesc  -> a QComboBox of its enumerators
//   rosetta::widget::*  -> which editor when several would fit
//                          (slider / spin / checkbox / color / radio /
//                           multiline / file / textfield)
//   rosetta::button     -> a push button in the action row
//
// Add a class to the manifest and its editor appears. Add a field and its row
// appears. Nothing here changes.

#pragma once

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include "../interp.h"

class PropertyPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertyPanel(QWidget *parent = nullptr) : QWidget(parent) {
        root_ = new QVBoxLayout(this);
        root_->setContentsMargins(8, 8, 8, 8);
        clear();
    }

    void clear() {
        auto *body = fresh_body();
        body->layout()->addWidget(new QLabel(QStringLiteral("<i>no selection</i>"), body));
        static_cast<QVBoxLayout *>(body->layout())->addStretch(1);
    }

    /** @brief Build the editor for `obj`. `name` is the interpreter variable. */
    void show_object(const QString &name, const dynui::rd::Object &obj) {
        obj_  = obj;
        name_ = name;
        if (!obj_.valid()) {
            clear();
            return;
        }
        auto *body = fresh_body();
        auto *col  = static_cast<QVBoxLayout *>(body->layout());

        const dynui::rd::MetaClass &k = *obj_.meta();
        col->addWidget(
            new QLabel(QStringLiteral("<b>%1</b> &nbsp;<span style='color:gray'>%2</span>")
                           .arg(name, QString::fromUtf8(k.qualified)),
                       body));
        if (*k.doc) {
            auto *d = new QLabel(QString::fromUtf8(k.doc), body);
            d->setStyleSheet("color:gray");
            d->setWordWrap(true);
            col->addWidget(d);
        }

        auto *form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignRight);
        for (std::size_t i = 0; i < k.n_fields; ++i) {
            add_row(body, form, k.fields[i]);
        }
        col->addLayout(form);

        add_actions(body, col, k);
        col->addStretch(1);
    }

    /** @brief Re-read every value without rebuilding the widgets. */
    void refresh() {
        if (!obj_.valid()) {
            return;
        }
        const QString n = name_;
        const auto    o = obj_;
        show_object(n, o); // simplest correct refresh for a demo-sized form
    }

signals:
    void changed();
    void message(const QString &text, bool is_error);
    /** @brief A nested object field was opened — the handle PINS its parent. */
    void drilled(const QString &name, const dynui::rd::Object &obj);

private:
    // -----------------------------------------------------------------------

    /**
     * @brief Drop the current form and start a new one.
     *
     * The whole panel lives inside a single child widget, so a rebuild is one
     * deleteLater() — no walking of nested layouts (which is what produced the
     * `QFormLayout::takeAt: Invalid index` warnings), and safe to call from
     * inside a signal emitted by one of the widgets being replaced.
     */
    QWidget *fresh_body() {
        if (body_) {
            body_->setParent(nullptr);
            body_->deleteLater();
        }
        body_ = new QWidget(this);
        auto *col = new QVBoxLayout(body_);
        col->setContentsMargins(0, 0, 0, 0);
        root_->addWidget(body_);
        return body_;
    }

    /** @brief Write a value back, reporting whatever the core says about it. */
    void write(const dynui::rd::MetaField &f, const dynui::rd::Any &v) {
        const auto r = obj_.set(f.name, v);
        if (!r.ok()) {
            emit message(QString::fromStdString(r.error), true);
            refresh(); // the widget is now out of sync with the object
        } else {
            emit changed();
        }
    }

    void add_row(QWidget *body, QFormLayout *form, const dynui::rd::MetaField &f) {
        const QString cap = QString::fromStdString(dynui::label_of(f));
        const QString tip = QString::fromUtf8(f.doc);
        QWidget      *w   = build_editor(body, f);
        if (!w) {
            return;
        }
        w->setToolTip(tip);
        w->setEnabled(!f.readonly && f.set != nullptr);

        auto *cell = w;
        // A bounded numeric field gets a live read-out next to its slider.
        if (auto *sl = qobject_cast<QSlider *>(w)) {
            auto *box  = new QWidget(body);
            auto *h    = new QHBoxLayout(box);
            auto *echo = new QLabel(box);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(sl, 1);
            h->addWidget(echo);
            echo->setMinimumWidth(52);
            // Seed from the OBJECT's value, not the slider's — the slider has
            // 1000 steps, so re-deriving would show 0.6978 for a field of 0.7.
            const auto cur = obj_.get(f.name);
            echo->setText(QString::number(cur.ok() ? cur.value.as_number() : 0.0, 'g', 4));
            connect(sl, &QSlider::valueChanged, echo, [echo, &f](int raw) {
                echo->setText(QString::number(slider_value(f, raw), 'g', 4));
            });
            cell = box;
        }

        auto *lab = new QLabel(cap + (f.readonly ? QStringLiteral(" 🔒") : QString()), body);
        lab->setToolTip(tip);
        form->addRow(lab, cell);
    }

    static double slider_value(const dynui::rd::MetaField &f, int raw) {
        return f.range.lo + (f.range.hi - f.range.lo) * raw / 1000.0;
    }

    QWidget *build_editor(QWidget *body, const dynui::rd::MetaField &f) {
        const std::string kind = dynui::widget_for(f);
        const auto        cur  = obj_.get(f.name);
        namespace rd           = dynui::rd;

        // ---- enum / string choices -> combo or radio group ----
        const auto choices = dynui::choices_for(f);
        if (!choices.empty()) {
            const bool is_enum = f.type->kind == rd::Kind::enum_;
            QString    now     = is_enum && cur.ok()
                                     ? QString::fromStdString(
                                       dynui::enumerator_name(f.type, cur.value.as_int()))
                                     : (cur.ok() && cur.value.kind() == rd::Kind::string
                                            ? QString::fromStdString(cur.value.as_string())
                                            : QString());
            if (kind == "radio") {
                auto *box = new QWidget(body);
                auto *h   = new QHBoxLayout(box);
                h->setContentsMargins(0, 0, 0, 0);
                for (const auto &c : choices) {
                    auto *rb = new QRadioButton(QString::fromStdString(c), box);
                    rb->setChecked(QString::fromStdString(c) == now);
                    const std::string cc = c;
                    connect(rb, &QRadioButton::toggled, this, [this, &f, cc](bool on) {
                        if (on) {
                            write(f, rd::Any::text(cc));
                        }
                    });
                    h->addWidget(rb);
                }
                h->addStretch(1);
                return box;
            }
            auto *cb = new QComboBox(body);
            for (const auto &c : choices) {
                cb->addItem(QString::fromStdString(c));
            }
            cb->setCurrentText(now);
            connect(cb, &QComboBox::currentIndexChanged, this, [this, &f, is_enum](int i) {
                if (i < 0) {
                    return;
                }
                // An enum is written by its VALUE, read from the TypeDesc —
                // the panel never hard-codes an enumeration it has not seen.
                write(f, is_enum ? rd::Any::enumeration(f.type->enumerators[i].value, f.type)
                                 : rd::Any::text(dynui::choices_for(f)[std::size_t(i)]));
            });
            return cb;
        }

        switch (f.type->kind) {
        case rd::Kind::boolean: {
            auto *cb = new QCheckBox(body);
            cb->setChecked(cur.ok() && cur.value.as_bool());
            connect(cb, &QCheckBox::toggled, this,
                    [this, &f](bool on) { write(f, rd::Any::boolean(on)); });
            return cb;
        }

        case rd::Kind::number: {
            const double now = cur.ok() ? cur.value.as_number() : 0.0;
            if (f.range.has && kind == "slider") {
                auto *sl = new QSlider(Qt::Horizontal, body);
                sl->setRange(0, 1000);
                const double span = f.range.hi - f.range.lo;
                sl->setValue(span > 0 ? int((now - f.range.lo) / span * 1000.0) : 0);
                connect(sl, &QSlider::valueChanged, this, [this, &f](int raw) {
                    const double v = slider_value(f, raw);
                    write(f, f.type->integral ? rd::Any::integer(static_cast<long long>(v))
                                              : rd::Any::real(v));
                });
                return sl;
            }
            if (f.type->integral) {
                auto *sb = new QSpinBox(body);
                sb->setRange(f.range.has ? int(f.range.lo) : -1000000,
                             f.range.has ? int(f.range.hi) : 1000000);
                sb->setValue(int(now));
                connect(sb, &QSpinBox::valueChanged, this,
                        [this, &f](int v) { write(f, rd::Any::integer(v)); });
                return sb;
            }
            auto *sb = new QDoubleSpinBox(body);
            sb->setDecimals(4);
            sb->setSingleStep(0.1);
            sb->setRange(f.range.has ? f.range.lo : -1e9, f.range.has ? f.range.hi : 1e9);
            sb->setValue(now);
            connect(sb, &QDoubleSpinBox::valueChanged, this,
                    [this, &f](double v) { write(f, rd::Any::real(v)); });
            return sb;
        }

        case rd::Kind::string: {
            const QString now =
                cur.ok() ? QString::fromStdString(cur.value.as_string()) : QString();
            if (kind == "color") {
                auto *btn = new QPushButton(now, body);
                btn->setStyleSheet(QStringLiteral("background:%1; color:#111").arg(now));
                connect(btn, &QPushButton::clicked, this, [this, &f, btn] {
                    const QColor c = QColorDialog::getColor(QColor(btn->text()), this);
                    if (c.isValid()) {
                        write(f, rd::Any::text(c.name().toStdString()));
                    }
                });
                return btn;
            }
            if (kind == "file") {
                auto *box  = new QWidget(body);
                auto *h    = new QHBoxLayout(box);
                auto *edit = new QLineEdit(now, box);
                auto *btn  = new QPushButton(QStringLiteral("…"), box);
                h->setContentsMargins(0, 0, 0, 0);
                h->addWidget(edit, 1);
                h->addWidget(btn);
                connect(btn, &QPushButton::clicked, this, [this, &f, edit] {
                    const QString p = QFileDialog::getOpenFileName(this);
                    if (!p.isEmpty()) {
                        edit->setText(p);
                        write(f, rd::Any::text(p.toStdString()));
                    }
                });
                connect(edit, &QLineEdit::editingFinished, this, [this, &f, edit] {
                    write(f, rd::Any::text(edit->text().toStdString()));
                });
                return box;
            }
            if (kind == "multiline") {
                auto *te = new QPlainTextEdit(now, body);
                te->setMaximumHeight(72);
                connect(te, &QPlainTextEdit::textChanged, this, [this, &f, te] {
                    write(f, rd::Any::text(te->toPlainText().toStdString()));
                });
                return te;
            }
            auto *le = new QLineEdit(now, body);
            connect(le, &QLineEdit::editingFinished, this, [this, &f, le] {
                write(f, rd::Any::text(le->text().toStdString()));
            });
            return le;
        }

        case rd::Kind::object: {
            // Drill into a nested bound object. The handle carries the parent's
            // owner, so editing the sub-form writes through to the parent and
            // the parent cannot be destroyed while the sub-form is open.
            auto *btn = new QPushButton(
                QString::fromUtf8(f.type->spelling) + QStringLiteral("  ›"), body);
            btn->setEnabled(true);
            connect(btn, &QPushButton::clicked, this, [this, &f] {
                const auto r = obj_.get(f.name);
                if (!r.ok() || !r.value.as_object().cls) {
                    emit message(QString::fromStdString(r.error), true);
                    return;
                }
                const auto &ref = r.value.as_object();
                emit drilled(name_ + "." + QString::fromUtf8(f.name),
                             dynui::rd::Object::adopt(*ref.cls, ref.ptr, ref.owner));
            });
            return btn;
        }

        case rd::Kind::vector: {
            auto *lab = new QLabel(
                cur.ok() ? QString::fromStdString(cur.value.to_string()) : QString(), body);
            lab->setStyleSheet("color:gray");
            return lab;
        }

        default: {
            auto *lab = new QLabel(QStringLiteral("<i>%1</i>").arg(f.type->spelling), body);
            lab->setStyleSheet("color:gray");
            return lab;
        }
        }
    }

    void add_actions(QWidget *body, QVBoxLayout *col, const dynui::rd::MetaClass &k) {
        auto *box  = new QGroupBox(QStringLiteral("Actions"), body);
        auto *h    = new QHBoxLayout(box);
        int   count = 0;
        for (std::size_t i = 0; i < k.n_methods; ++i) {
            const dynui::rd::MetaMethod &m = k.methods[i];
            const std::string caption = dynui::ann(m.annotations, m.n_annotations, "button");
            // Only nullary methods can be one-click actions; anything taking
            // arguments belongs in the console, where they can be supplied.
            if (caption.empty() || !m.invoke || m.n_params != 0) {
                continue;
            }
            auto *btn = new QPushButton(QString::fromStdString(caption), box);
            btn->setToolTip(QString::fromUtf8(m.doc));
            const std::string mname = m.name;
            connect(btn, &QPushButton::clicked, this, [this, mname] {
                const auto r = obj_.call(mname);
                if (!r.ok()) {
                    emit message(QString::fromStdString(r.error), true);
                } else if (r.value.kind() != dynui::rd::Kind::void_) {
                    emit message(QString::fromStdString(r.value.to_string()), false);
                }
                emit changed();
                refresh();
            });
            h->addWidget(btn);
            ++count;
        }
        h->addStretch(1);
        if (count) {
            col->addWidget(box);
        } else {
            box->deleteLater();
        }
    }

    QVBoxLayout      *root_  = nullptr;
    QWidget          *body_ = nullptr;
    dynui::rd::Object obj_;
    QString           name_;
};
