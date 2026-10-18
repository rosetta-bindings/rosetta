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
#include <QApplication>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include "../interp.h"

/** @brief A numeric vector as a small line plot (rosetta::widget::plot). */
class SparkLine : public QWidget {
public:
    SparkLine(std::vector<double> v, QWidget *parent) : QWidget(parent), v_(std::move(v)) {
        setMinimumSize(160, 56); // a QFormLayout gives a hint-less widget no width
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base());
        if (v_.size() < 2) {
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("(no data)"));
            return;
        }
        const auto [lo, hi] = std::minmax_element(v_.begin(), v_.end());
        const double span   = *hi > *lo ? *hi - *lo : 1;
        QPolygonF    line;
        for (std::size_t i = 0; i < v_.size(); ++i) {
            line << QPointF(width() * double(i) / double(v_.size() - 1),
                            height() - 4 - (height() - 8) * (v_[i] - *lo) / span);
        }
        p.setPen(QPen(palette().highlight().color(), 1.5));
        p.drawPolyline(line);
    }

private:
    std::vector<double> v_;
};

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

        // rosetta::order sorts the rows (unordered ones keep declaration order,
        // after the ordered ones); rosetta::group opens a section.
        std::vector<const dynui::rd::MetaField *> fields;
        for (std::size_t i = 0; i < k.n_fields; ++i) {
            fields.push_back(&k.fields[i]);
        }
        std::stable_sort(fields.begin(), fields.end(), [](auto *a, auto *b) {
            const char *oa = a->annotation("order");
            const char *ob = b->annotation("order");
            if (!oa || !ob) {
                return oa && !ob;
            }
            return std::atoi(oa) < std::atoi(ob);
        });
        // Then cluster by rosetta::group, groups in order of first appearance,
        // so a section is never split in two.
        std::vector<std::string> groups;
        for (const auto *f : fields) {
            const std::string g = f->annotation("group") ? f->annotation("group") : "";
            if (std::find(groups.begin(), groups.end(), g) == groups.end()) {
                groups.push_back(g);
            }
        }
        std::stable_sort(fields.begin(), fields.end(), [&groups](auto *a, auto *b) {
            const auto rank = [&groups](const dynui::rd::MetaField *f) {
                const std::string g = f->annotation("group") ? f->annotation("group") : "";
                return std::find(groups.begin(), groups.end(), g) - groups.begin();
            };
            return rank(a) < rank(b);
        });
        std::string group;
        for (const auto *f : fields) {
            if (const char *g = f->annotation("group"); g && group != g) {
                group     = g;
                auto *sec = new QLabel(QStringLiteral("<b>%1</b>").arg(QString::fromUtf8(g)), body);
                sec->setContentsMargins(0, 6, 0, 0);
                form->addRow(sec);
            }
            const int before = form->rowCount();
            add_row(body, form, *f);
            if (form->rowCount() == before) {
                continue;
            }
            const int row = form->rowCount() - 1;
            // enabled_if / visible_if, evaluated by the dynamic core against the
            // live object; the panel is rebuilt after every edit, so they track it.
            if (const char *e = f->annotation("visible_if")) {
                form->setRowVisible(row, dynui::rd::condition(obj_, e));
            }
            if (const char *e = f->annotation("enabled_if")) {
                if (auto *it = form->itemAt(row, QFormLayout::FieldRole); it && it->widget()) {
                    it->widget()->setEnabled(it->widget()->isEnabled() &&
                                             dynui::rd::condition(obj_, e));
                }
            }
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

    /** @brief Doc plus the annotation facts (unit, bounds, pattern, ...) for a tooltip. */
    template <class M> static QString tip_of(const M &m) {
        QString tip = QString::fromUtf8(m.doc);
        for (std::size_t i = 0; i < m.n_annotations; ++i) {
            const std::string key = m.annotations[i].key;
            static const char *shown[] = {"unit",     "min",        "max",   "length",
                                          "regex",    "requires",   "example", "deprecated",
                                          "since",    "expensive",  "confirm"};
            if (std::find(std::begin(shown), std::end(shown), key) != std::end(shown)) {
                tip += (tip.isEmpty() ? "" : "\n") + QString::fromStdString(key) + ": " +
                       QString::fromUtf8(m.annotations[i].value);
            }
        }
        return tip;
    }

    /** @brief The row caption: label, unit, deprecation. */
    template <class M> static QString caption_of(const M &m, QString cap) {
        if (const char *u = m.annotation("unit")) {
            cap += QStringLiteral(" [%1]").arg(QString::fromUtf8(u));
        }
        if (m.annotation("deprecated")) {
            cap += QStringLiteral(" (deprecated)");
        }
        return cap;
    }

    void add_row(QWidget *body, QFormLayout *form, const dynui::rd::MetaField &f) {
        const QString cap = caption_of(f, QString::fromStdString(dynui::label_of(f)));
        const QString tip = tip_of(f);
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

    // rosetta::log_scale: the slider moves along log10 of the value.
    static bool log_slider(const dynui::rd::MetaField &f) {
        return f.annotation("log_scale") && f.range.lo > 0 && f.range.hi > f.range.lo;
    }

    static double slider_value(const dynui::rd::MetaField &f, int raw) {
        if (log_slider(f)) {
            return f.range.lo * std::pow(f.range.hi / f.range.lo, raw / 1000.0);
        }
        return f.range.lo + (f.range.hi - f.range.lo) * raw / 1000.0;
    }

    static int slider_raw(const dynui::rd::MetaField &f, double v) {
        if (log_slider(f)) {
            return int(1000.0 * std::log(v / f.range.lo) / std::log(f.range.hi / f.range.lo));
        }
        const double span = f.range.hi - f.range.lo;
        return span > 0 ? int((v - f.range.lo) / span * 1000.0) : 0;
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
            if (f.range.has && kind == "progress") { // a read-out, not an editor
                auto        *pb = new QProgressBar(body);
                const double fr = (now - f.range.lo) / (f.range.hi - f.range.lo);
                pb->setRange(0, 1000);
                pb->setValue(int(1000 * std::clamp(fr, 0.0, 1.0)));
                pb->setFormat(QString::number(now, 'g', 4));
                return pb;
            }
            if (f.range.has && kind == "slider") {
                auto *sl = new QSlider(Qt::Horizontal, body);
                sl->setRange(0, 1000);
                sl->setValue(slider_raw(f, now));
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
                if (const char *st = f.annotation("step")) {
                    sb->setSingleStep(std::max(1, std::atoi(st)));
                }
                sb->setValue(int(now));
                connect(sb, &QSpinBox::valueChanged, this,
                        [this, &f](int v) { write(f, rd::Any::integer(v)); });
                return sb;
            }
            auto       *sb = new QDoubleSpinBox(body);
            const char *dc = f.annotation("decimals");
            const char *st = f.annotation("step");
            sb->setDecimals(dc ? std::atoi(dc) : 4);
            sb->setSingleStep(st ? std::atof(st) : 0.1);
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
            if (kind == "plot" && cur.ok()) {
                std::vector<double> v;
                for (const auto &e : cur.value.as_list()) {
                    if (e.kind() == rd::Kind::number) {
                        v.push_back(e.as_number());
                    }
                }
                return new SparkLine(std::move(v), body);
            }
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
            auto *btn = new QPushButton(caption_of(m, QString::fromStdString(caption)), box);
            btn->setToolTip(tip_of(m));
            const std::string mname    = m.name;
            const char       *question = m.annotation("confirm");
            if (!question && m.annotation("destructive")) {
                question = "Run this? It cannot be undone.";
            }
            const bool slow = m.annotation("expensive") != nullptr;
            connect(btn, &QPushButton::clicked, this, [this, mname, question, slow] {
                // rosetta::confirm / destructive: ask first.
                if (question && QMessageBox::question(this, QStringLiteral("Confirm"),
                                                      QString::fromUtf8(question)) !=
                                    QMessageBox::Yes) {
                    return;
                }
                if (slow) { // rosetta::expensive: say so while it runs
                    QApplication::setOverrideCursor(Qt::WaitCursor);
                }
                const auto r = obj_.call(mname);
                if (slow) {
                    QApplication::restoreOverrideCursor();
                }
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
