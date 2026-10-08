// Copyright (c) fmaerten@gmail.com
// License: MIT

// The window: a 3D view, a generated property panel, an object list, and a
// console — all driven by the same dynui::Interp over the same metadata.
//
// The two halves stay in sync through one signal each way. A console command
// mutates an object, the panel and the view re-read. A slider drag mutates an
// object, the view re-reads. Neither path knows what class it is touching.
//
// Even the "Add" menu is metadata-driven: it is built by scanning the registry
// for static, nullary-or-defaultable factory methods that RETURN a drawable
// class. Bind a library with different factories and the menu changes.

#pragma once

#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>

#include <rosetta/runtime/mcp_qt_http.h>

#include "claudepanel.h"
#include "console.h"
#include "propertypanel.h"
#include "sceneview.h"

#if ROSETTA_MCP_LUA
#include <rosetta/runtime/mcp_lua.h> // run_script for the Claude panel
#endif
#if ROSETTA_MCP_PYTHON
#include <rosetta/runtime/mcp_python.h> // run_python for the Claude panel
#endif

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow() {
        setWindowTitle(QStringLiteral("rosetta — dynamic object model"));
        resize(1180, 780);

        view_    = new SceneView(&interp_, this);
        panel_   = new PropertyPanel(this);
        console_ = new Console(&interp_, this);
        objects_ = new QListWidget(this);

#if ROSETTA_MCP_LUA
        rosetta::mcp::enable_lua(mcp_);
#endif
#if ROSETTA_MCP_PYTHON
        rosetta::mcp::enable_python(mcp_);
#endif
        claude_ = new ClaudePanel(&interp_, &mcp_, this);

        setCentralWidget(view_);

        auto *left = new QDockWidget(QStringLiteral("Objects"), this);
        left->setWidget(objects_);
        addDockWidget(Qt::LeftDockWidgetArea, left);

        auto *right = new QDockWidget(QStringLiteral("Properties"), this);
        right->setWidget(panel_);
        right->setMinimumWidth(340);
        addDockWidget(Qt::RightDockWidgetArea, right);

        auto *bottom = new QDockWidget(QStringLiteral("Console"), this);
        bottom->setWidget(console_);
        addDockWidget(Qt::BottomDockWidgetArea, bottom);

        // Claude shares the bottom area with the console, as a second tab:
        // two ways of driving the same objects.
        auto *chat = new QDockWidget(QStringLiteral("Claude"), this);
        chat->setWidget(claude_);
        addDockWidget(Qt::BottomDockWidgetArea, chat);
        tabifyDockWidget(bottom, chat);
        bottom->raise();

        // ---- the interpreter talks to the UI, not the other way round ----
        interp_.out = [this](const std::string &s, bool is_error) {
            console_->write(QString::fromStdString(s), is_error);
        };
        interp_.changed = [this] { refresh_all(); };

        connect(console_, &Console::executed, this, &MainWindow::refresh_all);
        connect(claude_, &ClaudePanel::sceneChanged, this, [this] {
            refresh_all();
            view_->repaint(); // show each step of a multi-call turn, not just the last
        });
        connect(view_, &SceneView::stats, this, [this](int objs, int tris, double ms) {
            statusBar()->showMessage(
                QStringLiteral(
                    "%1 object(s), %2 class(es) in the registry  |  drawing %3 mesh(es), "
                    "%4 triangles in %5 ms")
                    .arg(interp_.vars.size())
                    .arg(dynui::rd::registry().classes().size())
                    .arg(objs)
                    .arg(tris)
                    .arg(ms, 0, 'f', 2));
        });
        connect(panel_, &PropertyPanel::changed, this, [this] { view_->update(); });
        connect(panel_, &PropertyPanel::message, console_, &Console::write);
        connect(panel_, &PropertyPanel::drilled, this,
                [this](const QString &n, const dynui::rd::Object &o) {
                    panel_->show_object(n, o);
                    console_->write(QStringLiteral("editing %1 (pinned to its parent)").arg(n));
                });
        connect(objects_, &QListWidget::currentItemChanged, this,
                [this](QListWidgetItem *cur, QListWidgetItem *) {
                    select(cur ? cur->data(Qt::UserRole).toString() : QString());
                });

        build_menus();
        start_mcp_host();

        console_->write(QStringLiteral(
            "Metadata loaded. Everything in this window was built by querying it — "
            "no generated UI code."));
        console_->run(QStringLiteral("classes"));

        // A starting scene, created exactly the way a user would — including
        // passing one bound class (Vec3) to another's method (Mesh::translate).
        console_->run(QStringLiteral("static bunny scene::Mesh bunny"));
        console_->run(QStringLiteral("set bunny size 1.6"));
        console_->run(QStringLiteral("static cube scene::Mesh cube"));
        console_->run(QStringLiteral("set cube shading Wireframe"));
        console_->run(QStringLiteral("new left scene::Vec3 -1.3 0 0"));
        console_->run(QStringLiteral("call cube translate $left"));
        console_->run(QStringLiteral("static sphere scene::Mesh sphere 16 24"));
        console_->run(QStringLiteral("set sphere colour #e5a04c"));
        console_->run(QStringLiteral("set sphere size 0.7"));
        console_->run(QStringLiteral("new right scene::Vec3 1.3 0 0"));
        console_->run(QStringLiteral("call sphere translate $right"));
        refresh_all();
        if (objects_->count()) {
            objects_->setCurrentRow(0);
        }
    }

    /** @brief Ask Claude from outside (used by `--claude`); raises its tab. */
    void ask_claude(const QString &text) {
        if (auto *dock = qobject_cast<QDockWidget *>(claude_->parentWidget())) {
            dock->raise();
        }
        claude_->ask(text);
    }

    ClaudePanel *claude() const { return claude_; }

    /** @brief Run one console command from outside (used by `--run`). */
    void command(const QString &cmd) {
        console_->run(cmd);
        refresh_all();
    }

private slots:
    void select(const QString &name) {
        auto it = interp_.vars.find(name.toStdString());
        if (it == interp_.vars.end()) {
            panel_->clear();
            return;
        }
        panel_->show_object(name, it->second);
    }

    void refresh_all() {
        const QString keep = objects_->currentItem() ? objects_->currentItem()->text() : QString();
        objects_->blockSignals(true);
        objects_->clear();
        for (const auto &[n, o] : interp_.vars) {
            const bool drawable = o.meta() && dynui::has_geometry(*o.meta());
            auto      *item     = new QListWidgetItem(QString::fromStdString(n), objects_);
            item->setToolTip(QString::fromStdString(o.as_any().to_string()));
            if (drawable) {
                item->setText(QString::fromStdString(n) + QStringLiteral("  ◼"));
            }
            // The list stores the plain name; the marker is display only.
            item->setData(Qt::UserRole, QString::fromStdString(n));
        }
        objects_->blockSignals(false);

        for (int i = 0; i < objects_->count(); ++i) {
            if (objects_->item(i)->data(Qt::UserRole).toString() == keep) {
                objects_->setCurrentRow(i);
                break;
            }
        }
        // With nothing selected, prefer the first DRAWABLE object: in a 3D app a
        // bare Vec3 is a poor default just because its name sorts first.
        if (!objects_->currentItem() && objects_->count()) {
            int pick = 0;
            for (int i = 0; i < objects_->count(); ++i) {
                const auto it = interp_.vars.find(
                    objects_->item(i)->data(Qt::UserRole).toString().toStdString());
                if (it != interp_.vars.end() && it->second.meta() &&
                    dynui::has_geometry(*it->second.meta())) {
                    pick = i;
                    break;
                }
            }
            objects_->setCurrentRow(pick);
        }
        // Rebuild the panel against the (possibly changed) selection.
        if (objects_->currentItem()) {
            select(objects_->currentItem()->data(Qt::UserRole).toString());
        }
        view_->update(); // the status line is refreshed from SceneView::stats
    }

private:
    /**
     * @brief Serve this window's scene to MCP clients over HTTP.
     *
     * The same in-process server the Claude panel uses, reachable from outside
     * — so Claude Code (or any MCP client) drives the objects this window
     * draws. Port from ROSETTA_MCP_PORT (default 8770; 0 disables).
     */
    void start_mcp_host() {
        const quint16 port =
            quint16(qEnvironmentVariableIntValue("ROSETTA_MCP_PORT") ?: 8770);
        if (qEnvironmentVariableIsSet("ROSETTA_MCP_PORT") &&
            qEnvironmentVariableIntValue("ROSETTA_MCP_PORT") == 0) {
            return;
        }
        host_.set_handler([this](const rosetta::mcp::json &msg) {
            const bool is_call = msg.value("method", "") == "tools/call";
            if (is_call) {
                scenesync::publish(interp_, mcp_);
            }
            std::optional<rosetta::mcp::json> out = mcp_.handle(msg);
            if (is_call) {
                scenesync::adopt(interp_, mcp_);
                log_mcp_call(msg, out);
                refresh_all();
                view_->repaint();
            }
            return out;
        });

        auto *badge = new QLabel(this);
        statusBar()->addPermanentWidget(badge);
        if (!host_.listen(port)) {
            badge->setText(QStringLiteral("MCP: off"));
            console_->write(QStringLiteral("MCP host could not listen on port %1: %2 "
                                           "(set ROSETTA_MCP_PORT to another port)")
                                .arg(port)
                                .arg(host_.error()),
                            true);
            return;
        }
        badge->setText(QStringLiteral("MCP: %1").arg(host_.url()));
        claude_->set_mcp_url(host_.url()); // the Claude tab's "Claude Code" backend uses it
        console_->write(QStringLiteral("MCP server for this scene: %1\n"
                                       "  connect Claude Code with:  claude mcp add --transport "
                                       "http scene-viewer %1")
                            .arg(host_.url()));
    }

    /** @brief One console line per tool call an outside client makes. */
    void log_mcp_call(const rosetta::mcp::json &msg, const std::optional<rosetta::mcp::json> &out) {
        const auto &params = msg.contains("params") ? msg["params"] : rosetta::mcp::json::object();
        QString     line   = QStringLiteral("[mcp] ") +
                       QString::fromStdString(params.value("name", std::string("?")));
        const auto args = params.value("arguments", rosetta::mcp::json::object());
        QString    detail;
        if (args.contains("code")) {
            detail = QStringLiteral("(%1 line script)")
                         .arg(QString::fromStdString(args["code"].get<std::string>()).count('\n') + 1);
        } else {
            detail = QString::fromStdString(args.dump());
        }
        if (detail.size() > 160) {
            detail = detail.left(160) + QStringLiteral("…");
        }
        const bool failed = !out || out->contains("error") ||
                            (*out)["result"].value("isError", false);
        console_->write(line + QStringLiteral(" ") + detail + (failed ? QStringLiteral("  ✗") : QString()),
                        failed);
    }

    /**
     * @brief Build the "Add" menu from the registry.
     *
     * Looks for static methods whose return type is a DRAWABLE class and whose
     * parameters are all numbers (so the menu can supply defaults). Nothing
     * here is specific to Mesh, cube, plane or sphere.
     */
    void build_menus() {
        auto *add = menuBar()->addMenu(QStringLiteral("&Add"));
        auto *bar = addToolBar(QStringLiteral("Add"));

        for (const dynui::rd::MetaClass *k : dynui::rd::registry().classes()) {
            for (std::size_t i = 0; i < k->n_methods; ++i) {
                const dynui::rd::MetaMethod &m = k->methods[i];
                if (!m.is_static || !m.invoke || !m.ret || m.ret->kind != dynui::rd::Kind::object) {
                    continue;
                }
                if (!m.ret->cls || !dynui::has_geometry(*m.ret->cls)) {
                    continue;
                }
                bool numeric = true;
                for (std::size_t p = 0; p < m.n_params; ++p) {
                    numeric = numeric && m.params[p].type->kind == dynui::rd::Kind::number;
                }
                if (!numeric) {
                    continue;
                }

                const QString  cls  = QString::fromUtf8(k->qualified);
                const QString  meth = QString::fromUtf8(m.name);
                const QString  doc  = QString::fromUtf8(m.doc);
                QStringList    args;
                for (std::size_t p = 0; p < m.n_params; ++p) {
                    // A plausible default for an unnamed numeric parameter.
                    args << (m.params[p].type->integral ? QStringLiteral("16")
                                                        : QStringLiteral("1"));
                }
                QAction *act = add->addAction(meth);
                act->setStatusTip(doc);
                bar->addAction(act);
                connect(act, &QAction::triggered, this, [this, cls, meth, args] {
                    const QString var = QString::fromStdString(
                        interp_.fresh(meth.toStdString()));
                    console_->run(QStringLiteral("static %1 %2 %3 %4")
                                      .arg(var, cls, meth, args.join(' '))
                                      .trimmed());
                });
            }
        }

        auto *help = menuBar()->addMenu(QStringLiteral("&Help"));
        help->addAction(QStringLiteral("Show classes"),
                        [this] { console_->run(QStringLiteral("classes")); });
        help->addAction(QStringLiteral("Show variables"),
                        [this] { console_->run(QStringLiteral("vars")); });
    }

    dynui::Interp        interp_;
    rosetta::mcp::Server mcp_{rosetta::mcp::Options{.name = "scene-viewer"}};
    rosetta::mcp::QtHttpHost host_{mcp_};
    ClaudePanel         *claude_ = nullptr;
    SceneView           *view_   = nullptr;
    PropertyPanel *panel_   = nullptr;
    Console       *console_ = nullptr;
    QListWidget   *objects_ = nullptr;
};
