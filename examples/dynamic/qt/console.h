// SPDX-FileCopyrightText: Copyright (c) fmaerten@gmail.com
// SPDX-License-Identifier: UNLICENSED

// A console for the dynamic interpreter: a transcript plus an input line with
// history and Tab completion.
//
// Completion is worth a look — it is built by querying the metadata, so it
// completes class names, variable names, and the fields and methods of
// whatever the variable in the current line happens to hold. No table of names
// is generated anywhere.

#pragma once

#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include "../interp.h"

/** @brief A QLineEdit with shell-style history on Up/Down. */
class CommandLine : public QLineEdit {
    Q_OBJECT

public:
    using QLineEdit::QLineEdit;

    void remember(const QString &s) {
        if (!s.isEmpty() && (history_.isEmpty() || history_.last() != s)) {
            history_ << s;
        }
        cursor_ = history_.size();
    }

protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Up && cursor_ > 0) {
            setText(history_.value(--cursor_));
            return;
        }
        if (e->key() == Qt::Key_Down) {
            cursor_ = qMin(cursor_ + 1, history_.size());
            setText(cursor_ < history_.size() ? history_.value(cursor_) : QString());
            return;
        }
        QLineEdit::keyPressEvent(e);
    }

private:
    QStringList history_;
    int         cursor_ = 0;
};

class Console : public QWidget {
    Q_OBJECT

public:
    explicit Console(dynui::Interp *interp, QWidget *parent = nullptr)
        : QWidget(parent), interp_(interp) {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(4, 4, 4, 4);

        log_ = new QPlainTextEdit(this);
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(4000);
        log_->setStyleSheet("font-family: Menlo, Consolas, monospace; font-size: 12px;");

        input_ = new CommandLine(this);
        input_->setPlaceholderText(
            QStringLiteral("classes | vars | static m scene::Mesh cube | set m spin 45 | "
                           "call m subdivide     (Tab completes, ↑ recalls)"));
        input_->setStyleSheet("font-family: Menlo, Consolas, monospace;");

        v->addWidget(log_, 1);
        v->addWidget(input_);

        connect(input_, &QLineEdit::returnPressed, this, &Console::submit);
    }

    void write(const QString &text, bool is_error = false) {
        log_->appendHtml(is_error
                             ? QStringLiteral("<span style='color:#e06c75'>! %1</span>")
                                   .arg(text.toHtmlEscaped())
                             : QStringLiteral("<span style='color:#c8ccd4'>%1</span>")
                                   .arg(text.toHtmlEscaped()));
    }

    void echo(const QString &cmd) {
        log_->appendHtml(QStringLiteral("<span style='color:#61afef'>&gt; %1</span>")
                             .arg(cmd.toHtmlEscaped()));
    }

    void run(const QString &cmd) {
        echo(cmd);
        interp_->run(cmd.toStdString());
    }

signals:
    void executed();

protected:
    /** @brief Tab completion, entirely metadata-driven. */
    bool event(QEvent *e) override {
        if (e->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(e);
            if (ke->key() == Qt::Key_Tab && input_->hasFocus()) {
                complete();
                return true;
            }
        }
        return QWidget::event(e);
    }

private:
    void submit() {
        const QString cmd = input_->text().trimmed();
        if (cmd.isEmpty()) {
            return;
        }
        input_->remember(cmd);
        input_->clear();
        run(cmd);
        emit executed();
    }

    void complete() {
        const QStringList tok  = input_->text().split(' ', Qt::SkipEmptyParts);
        const bool        open = input_->text().endsWith(' ');
        const int         pos  = tok.size() + (open ? 1 : 0);
        QStringList       pool;

        const QString cmd = tok.value(0);
        if (pos <= 1) {
            pool = {"classes", "vars",   "new",  "methods", "get",
                    "set",     "call",   "static", "del"};
        } else if (cmd == "new" && pos == 3) {
            pool = class_names();
        } else if (cmd == "static" && pos == 3) {
            pool = class_names();
        } else if (cmd == "methods" && pos == 2) {
            pool = class_names() + var_names();
        } else if ((cmd == "get" || cmd == "set" || cmd == "call" || cmd == "del") && pos == 2) {
            pool = var_names();
        } else if ((cmd == "get" || cmd == "set") && pos == 3) {
            pool = member_names(tok.value(1), /*fields=*/true);
        } else if (cmd == "call" && pos == 3) {
            pool = member_names(tok.value(1), /*fields=*/false);
        } else if (cmd == "static" && pos == 4) {
            pool = static_names(tok.value(2));
        }

        const QString prefix = open ? QString() : tok.value(tok.size() - 1);
        QStringList   hits;
        for (const QString &c : pool) {
            if (c.startsWith(prefix)) {
                hits << c;
            }
        }
        if (hits.isEmpty()) {
            return;
        }
        if (hits.size() == 1) {
            QStringList head = tok;
            if (!open && !head.isEmpty()) {
                head.removeLast();
            }
            head << hits.first();
            input_->setText(head.join(' ') + ' ');
        } else {
            write(hits.join("  "));
        }
    }

    static QStringList class_names() {
        QStringList out;
        for (const dynui::rd::MetaClass *k : dynui::rd::registry().classes()) {
            out << QString::fromUtf8(k->qualified);
        }
        return out;
    }

    QStringList var_names() const {
        QStringList out;
        for (const auto &[n, o] : interp_->vars) {
            out << QString::fromStdString(n);
        }
        return out;
    }

    QStringList member_names(const QString &varName, bool fields) const {
        QStringList out;
        auto        it = interp_->vars.find(varName.toStdString());
        if (it == interp_->vars.end() || !it->second.meta()) {
            return out;
        }
        const dynui::rd::MetaClass &k = *it->second.meta();
        if (fields) {
            for (std::size_t i = 0; i < k.n_fields; ++i) {
                out << QString::fromUtf8(k.fields[i].name);
            }
        } else {
            for (std::size_t i = 0; i < k.n_methods; ++i) {
                if (k.methods[i].invoke && !out.contains(QString::fromUtf8(k.methods[i].name))) {
                    out << QString::fromUtf8(k.methods[i].name);
                }
            }
        }
        return out;
    }

    static QStringList static_names(const QString &cls) {
        QStringList                 out;
        const dynui::rd::MetaClass *k = dynui::rd::registry().find_class(cls.toStdString());
        for (std::size_t i = 0; k && i < k->n_methods; ++i) {
            if (k->methods[i].is_static && k->methods[i].invoke) {
                out << QString::fromUtf8(k->methods[i].name);
            }
        }
        return out;
    }

    dynui::Interp  *interp_ = nullptr;
    QPlainTextEdit *log_    = nullptr;
    CommandLine    *input_  = nullptr;
};
