// Copyright (c) fmaerten@gmail.com
// License: MIT

// A chat panel connected to Claude, driving the SAME objects the 3D view draws.
//
// No MCP transport is involved: the panel holds an in-process
// rosetta::mcp::Server and offers its tools (list_classes, describe_class,
// call_method, set_fields, run_script, ...) to the Claude Messages API. When
// Claude calls one, the panel runs it right here, on the GUI thread, against
// the live scene — then the view and the property panel refresh, so you watch
// the bunny change while Claude works.
//
// The scene and the server share one set of objects. Before every tool call
// the server's handle table is rebuilt from the interpreter's variables (so
// Claude sees `bunny`, `cube`, ... under the names the console uses), and
// after it the interpreter's variables are rebuilt from the handle table (so
// an object Claude creates appears in the object list and gets drawn). Both
// sides hold rosetta::dyn::Object handles that share ownership, so the swap
// never destroys anything that one of them still holds.
//
// Two backends, picked in the panel:
//
// CLAUDE CODE (your login) — what the VS Code extension does: the panel runs
// the Claude Code CLI (`claude -p ... --output-format stream-json`) for each
// message, with ANTHROPIC_API_KEY removed from its environment so your Claude
// Code login (subscription) is used. The CLI is given exactly one MCP server —
// this viewer's own HTTP host (--mcp-config, --strict-mcp-config) — so its tool
// calls come straight back into this process and land on the live scene. It
// gets NO built-in tools (--tools "", so no shell, no file edits) and may use
// the scene-viewer tools without prompting (--allowedTools, --permission-mode
// dontAsk). Follow-up messages continue the session (--resume). The stream
// (system/init, assistant, user tool_result, result events) feeds the same
// transcript.
//
// ANTHROPIC API (API key) — the panel calls the Messages API itself over raw
// HTTPS (QNetworkAccessManager; there is no official C++ SDK), running tools
// in process with the standard manual loop: send, run every tool_use block,
// return all tool_result blocks in one user message, repeat until the turn
// ends. The assistant's content is appended back verbatim, thinking blocks
// included. Every request opts into server-side fallbacks ("fallbacks":
// "default"): a turn a safety classifier declines is re-run on Anthropic's
// recommended fallback model instead of returned as a refusal.
//
// Configuration (environment):
//   ANTHROPIC_API_KEY      API backend (or ANTHROPIC_AUTH_TOKEN, sent as Bearer)
//   ANTHROPIC_BASE_URL     API backend, default https://api.anthropic.com
//   ROSETTA_CLAUDE_MODEL   model; default claude-opus-5 (API), Claude Code's own
//                          default (Claude Code)
//   ROSETTA_CLAUDE_CLI     path to the `claude` executable (default: found on PATH)

#pragma once

#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QStandardPaths>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include <rosetta/runtime/mcp.h>

#include "../interp.h"
#include "scenesync.h"

class ClaudePanel : public QWidget {
    Q_OBJECT

public:
    using json = rosetta::mcp::json;

    ClaudePanel(dynui::Interp *interp, rosetta::mcp::Server *server, QWidget *parent = nullptr)
        : QWidget(parent), interp_(interp), server_(server) {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(4, 4, 4, 4);

        log_ = new QTextBrowser(this);
        log_->setOpenExternalLinks(true);
        log_->setStyleSheet("font-size: 13px;");

        input_ = new QLineEdit(this);
        input_->setPlaceholderText(
            QStringLiteral("Ask Claude — e.g. \"relax the bunny until its worst triangle "
                           "is above 0.1, then make it glossy\""));
        send_  = new QPushButton(QStringLiteral("Send"), this);
        stop_  = new QPushButton(QStringLiteral("Stop"), this);
        retry_ = new QPushButton(QStringLiteral("Retry"), this);
        reset_ = new QPushButton(QStringLiteral("New chat"), this);
        stop_->setEnabled(false);
        retry_->setEnabled(false);
        status_ = new QLabel(this);
        status_->setStyleSheet("color: gray;");

        backend_ = new QComboBox(this);
        backend_->addItem(QStringLiteral("Claude Code (your login)"));
        backend_->addItem(QStringLiteral("Anthropic API (API key)"));
        backend_->setToolTip(QStringLiteral(
            "Claude Code: runs the `claude` CLI with your Claude Code login, connected to this "
            "viewer's MCP server.\nAnthropic API: calls the API directly with ANTHROPIC_API_KEY."));

        auto *row = new QHBoxLayout;
        row->addWidget(backend_);
        row->addWidget(input_, 1);
        row->addWidget(send_);
        row->addWidget(stop_);
        row->addWidget(retry_);
        row->addWidget(reset_);

        v->addWidget(log_, 1);
        v->addLayout(row);
        v->addWidget(status_);

        connect(input_, &QLineEdit::returnPressed, this, &ClaudePanel::submit);
        connect(send_, &QPushButton::clicked, this, &ClaudePanel::submit);
        connect(stop_, &QPushButton::clicked, this, &ClaudePanel::stop);
        connect(retry_, &QPushButton::clicked, this, [this] {
            retry_->setEnabled(false);
            request();
        });
        connect(reset_, &QPushButton::clicked, this, [this] {
            stop();
            messages_ = json::array();
            session_id_.clear();
            log_->clear();
            greet();
        });

        model_ = qEnvironmentVariable("ROSETTA_CLAUDE_MODEL", QStringLiteral("claude-opus-5"));
        base_  = qEnvironmentVariable("ANTHROPIC_BASE_URL", QStringLiteral("https://api.anthropic.com"));
        cli_   = qEnvironmentVariable("ROSETTA_CLAUDE_CLI");
        if (cli_.isEmpty()) {
            cli_ = QStandardPaths::findExecutable(QStringLiteral("claude"));
        }

        // Default: Claude Code when it is installed (no key needed), else the API.
        backend_->setCurrentIndex(!cli_.isEmpty() || !has_credentials() ? 0 : 1);
        connect(backend_, &QComboBox::currentIndexChanged, this, [this] {
            if (!busy_) {
                greet();
            }
        });
        greet();
    }

    /** @brief The viewer's MCP endpoint, for the Claude Code backend. */
    void set_mcp_url(const QString &url) { mcp_url_ = url; }

    /** @brief Send `text` as if typed (used by the viewer's --claude flag). */
    void ask(const QString &text) {
        input_->setText(text);
        submit();
    }

signals:
    /** @brief A tool call may have changed the scene: re-read it. */
    void sceneChanged();

    /** @brief A message has been fully answered (or failed, or was stopped). */
    void turnFinished();

private slots:
    void submit() {
        const QString text = input_->text().trimmed();
        if (text.isEmpty() || busy_) {
            return;
        }
        if (claude_code()) {
            if (cli_.isEmpty() || mcp_url_.isEmpty()) {
                note(cli_.isEmpty()
                         ? QStringLiteral("The `claude` CLI was not found on PATH (set "
                                          "ROSETTA_CLAUDE_CLI to its path).")
                         : QStringLiteral("The viewer's MCP server is not running, so Claude Code "
                                          "cannot reach the scene (see the Console tab)."),
                     "#c0392b");
                return;
            }
            input_->clear();
            bubble(QStringLiteral("You"), text, "#2e86c1");
            run_claude_code(text);
            return;
        }
        if (!has_credentials()) {
            note(QStringLiteral("This tab needs an API key (ANTHROPIC_API_KEY, set before "
                                "launching the viewer)."),
                 "#c0392b");
            no_key_hint();
            return;
        }
        input_->clear();
        bubble(QStringLiteral("You"), text, "#2e86c1");

        // After an interrupted tool loop the last message is a user turn of
        // tool results; the new text joins it rather than starting a second
        // consecutive user turn.
        json block = {{"type", "text"}, {"text", text.toStdString()}};
        if (!messages_.empty() && messages_.back()["role"] == "user") {
            messages_.back()["content"].push_back(block);
        } else {
            messages_.push_back({{"role", "user"}, {"content", json::array({block})}});
        }
        rounds_ = 0;
        request();
    }

    void stop() {
        cancelled_ = true;
        if (reply_) {
            reply_->abort();
        }
        if (proc_) {
            proc_->kill();
        }
    }

private:
    bool claude_code() const { return backend_->currentIndex() == 0; }

    // ---- the Claude Code backend ----------------------------------------

    static constexpr const char *k_server = "scene-viewer"; // the MCP server's name for the CLI

    void run_claude_code(const QString &text) {
        busy(true);
        cancelled_ = false;
        got_result_ = false;
        out_buf_.clear();

        const json mcp = {
            {"mcpServers", {{k_server, {{"type", "http"}, {"url", mcp_url_.toStdString()}}}}}};
        QStringList args = {
            QStringLiteral("-p"),
            QStringLiteral("--output-format"), QStringLiteral("stream-json"),
            QStringLiteral("--verbose"),
            // No built-in tools: no shell, no file edits — only the scene.
            QStringLiteral("--tools"), QString(),
            QStringLiteral("--strict-mcp-config"),
            QStringLiteral("--mcp-config"), QString::fromStdString(mcp.dump()),
            QStringLiteral("--allowedTools"), QStringLiteral("mcp__%1").arg(k_server),
            QStringLiteral("--permission-mode"), QStringLiteral("dontAsk"),
            QStringLiteral("--append-system-prompt"), QString::fromStdString(viewer_prompt()),
        };
        if (!session_id_.isEmpty()) {
            args << QStringLiteral("--resume") << session_id_;
        }
        if (qEnvironmentVariableIsSet("ROSETTA_CLAUDE_MODEL")) {
            args << QStringLiteral("--model") << model_;
        }

        // Your Claude Code login, not an API key that may have no credits.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("ANTHROPIC_API_KEY"));
        env.remove(QStringLiteral("ANTHROPIC_AUTH_TOKEN"));

        // A fixed working directory, so --resume finds the session again.
        const QString dir = QDir::tempPath() + QStringLiteral("/rosetta-scene-viewer");
        QDir().mkpath(dir);

        proc_ = new QProcess(this);
        proc_->setProcessEnvironment(env);
        proc_->setWorkingDirectory(dir);
        connect(proc_, &QProcess::readyReadStandardOutput, this, [this] {
            out_buf_ += proc_->readAllStandardOutput();
            int nl;
            while ((nl = out_buf_.indexOf('\n')) >= 0) {
                const QByteArray line = out_buf_.left(nl).trimmed();
                out_buf_.remove(0, nl + 1);
                if (!line.isEmpty()) {
                    on_event(json::parse(line.toStdString(), nullptr, false));
                }
            }
        });
        connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            const QString err = QString::fromUtf8(proc_->readAllStandardError()).trimmed();
            if (cancelled_) {
                note(QStringLiteral("Stopped."), "gray");
            } else if (!got_result_) {
                note(QStringLiteral("Claude Code exited (code %1) without a result%2")
                         .arg(code)
                         .arg(err.isEmpty() ? QStringLiteral(".") : QStringLiteral(": ") + err.right(600)),
                     "#c0392b");
            }
            proc_->deleteLater();
            proc_ = nullptr;
            finish();
        });
        connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) {
                note(QStringLiteral("Could not start %1.").arg(cli_), "#c0392b");
            }
        });

        // The prompt goes on stdin, so a message starting with '-' is never
        // mistaken for an option.
        proc_->start(cli_, args);
        proc_->write(text.toUtf8());
        proc_->closeWriteChannel();
    }

    /** @brief One stream-json event from the CLI. */
    void on_event(const json &ev) {
        if (ev.is_discarded() || !ev.is_object()) {
            return;
        }
        const std::string type = ev.value("type", "");
        if (ev.contains("session_id") && ev["session_id"].is_string()) {
            session_id_ = QString::fromStdString(ev["session_id"]);
        }

        if (type == "system" && ev.value("subtype", "") == "init") {
            // Say once which login is in use, and whether the scene is reachable.
            std::string status = "not listed";
            for (const json &srv : ev.value("mcp_servers", json::array())) {
                if (srv.value("name", "") == k_server) {
                    status = srv.value("status", "?");
                }
            }
            if (status != "connected") {
                note(QStringLiteral("Claude Code could not connect to this viewer's MCP server "
                                    "(%1).")
                         .arg(QString::fromStdString(status)),
                     "#c0392b");
            }
            status_->setText(QStringLiteral("Claude Code %1 · %2 · credentials: %3 · working…")
                                 .arg(QString::fromStdString(ev.value("claude_code_version", "")),
                                      QString::fromStdString(ev.value("model", "")),
                                      QString::fromStdString(ev.value("apiKeySource", "login"))));
        } else if (type == "assistant") {
            for (const json &b : ev["message"].value("content", json::array())) {
                const std::string bt = b.value("type", "");
                if (bt == "text") {
                    bubble(QStringLiteral("Claude"), QString::fromStdString(b["text"]), "#8e44ad");
                } else if (bt == "tool_use") {
                    QString name = QString::fromStdString(b.value("name", ""));
                    name.remove(QStringLiteral("mcp__%1__").arg(k_server));
                    tool_call(name, b.value("input", json::object()));
                }
            }
        } else if (type == "user") {
            const json &content = ev["message"].value("content", json::array());
            for (const json &b : content.is_array() ? content : json::array()) {
                if (b.value("type", "") != "tool_result") {
                    continue;
                }
                std::string text;
                const json &c = b.value("content", json());
                if (c.is_string()) {
                    text = c;
                } else if (c.is_array()) {
                    for (const json &part : c) {
                        text += part.value("text", "");
                    }
                }
                tool_result(QString::fromStdString(text), b.value("is_error", false));
            }
        } else if (type == "result") {
            got_result_ = true;
            if (ev.value("is_error", false) || ev.value("subtype", "") != "success") {
                note(QStringLiteral("Claude Code: %1")
                         .arg(QString::fromStdString(ev.value("result", ev.value("subtype", "error")))),
                     "#c0392b");
            }
            for (const json &d : ev.value("permission_denials", json::array())) {
                note(QStringLiteral("Not allowed here: %1")
                         .arg(QString::fromStdString(d.value("tool_name", "?"))),
                     "gray");
            }
        }
    }

    std::string viewer_prompt() const {
        return "You are driving a live desktop 3D viewer for a C++ scene library through the "
               "scene-viewer MCP tools. The user sees a 3D view, an object list and a property "
               "panel; every change you make through those tools is shown live. Objects already "
               "in the scene are available under the names in the object list (list_objects). "
               "Prefer run_script for loops and searches. You have no other tools. Keep replies "
               "short: say what you changed and the numbers that matter.";
    }

    // ---- the API backend ---------------------------------------------------

    bool has_credentials() const {
        return !qEnvironmentVariableIsEmpty("ANTHROPIC_API_KEY") ||
               !qEnvironmentVariableIsEmpty("ANTHROPIC_AUTH_TOKEN");
    }

    std::string system_prompt() const {
        return "You are embedded in a desktop 3D viewer for a C++ scene library, bound through "
               "rosetta's dynamic object model. The user sees a 3D view, an object list and a "
               "property panel; every change you make to an object through the tools is shown "
               "live. Objects already in the scene are available under the names shown in the "
               "object list (call list_objects). Objects you create or keep() appear in the "
               "scene too; meshes are drawn. Prefer run_script for loops and searches — it is "
               "one tool call instead of many. Keep replies short: say what you changed and the "
               "numbers that matter.\n\n" +
               server_->instructions();
    }

    json tool_definitions() const {
        json out = json::array();
        for (const json &t : server_->tools()) {
            out.push_back({{"name", t["name"]},
                           {"description", t["description"]},
                           {"input_schema", t["inputSchema"]}});
        }
        return out;
    }

    void request() {
        if (++rounds_ > k_max_rounds) {
            note(QStringLiteral("Stopped after %1 tool rounds.").arg(k_max_rounds), "#c0392b");
            finish();
            return;
        }
        busy(true);
        cancelled_ = false;

        const json body = {
            {"model", model_.toStdString()},
            {"max_tokens", 16000},
            {"thinking", {{"type", "adaptive"}}},
            {"fallbacks", "default"},
            // Automatic prompt caching: the tools + system prefix and the
            // growing history are re-read from cache on every round.
            {"cache_control", {{"type", "ephemeral"}}},
            {"system", system_prompt()},
            {"tools", tool_definitions()},
            {"messages", messages_},
        };

        QNetworkRequest req(QUrl(base_ + QStringLiteral("/v1/messages")));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        req.setRawHeader("anthropic-version", "2023-06-01");
        QByteArray betas = "server-side-fallback-2026-07-01";
        if (!qEnvironmentVariableIsEmpty("ANTHROPIC_API_KEY")) {
            req.setRawHeader("x-api-key", qgetenv("ANTHROPIC_API_KEY"));
        } else {
            req.setRawHeader("Authorization", "Bearer " + qgetenv("ANTHROPIC_AUTH_TOKEN"));
            betas += ",oauth-2025-04-20";
        }
        req.setRawHeader("anthropic-beta", betas);
        req.setTransferTimeout(10 * 60 * 1000);

        reply_ = net_.post(req, QByteArray::fromStdString(body.dump()));
        connect(reply_, &QNetworkReply::finished, this, [this, r = reply_] { on_reply(r); });
    }

    void on_reply(QNetworkReply *r) {
        r->deleteLater();
        reply_ = nullptr;
        if (cancelled_) {
            note(QStringLiteral("Stopped."), "gray");
            finish(true);
            return;
        }

        const QByteArray raw    = r->readAll();
        const int        status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        json             resp   = json::parse(raw.toStdString(), nullptr, false);

        if (r->error() != QNetworkReply::NoError || status != 200 || resp.is_discarded()) {
            QString why = r->errorString();
            if (!resp.is_discarded() && resp.contains("error")) {
                why = QString::fromStdString(resp["error"].value("message", ""));
            }
            note(QStringLiteral("API error (HTTP %1): %2").arg(status).arg(why), "#c0392b");
            if (why.contains(QStringLiteral("credit balance"), Qt::CaseInsensitive)) {
                // The panel bills the API key. The viewer's MCP host does not:
                // Claude Code can drive this same scene on a subscription.
                note(QStringLiteral("This backend is billed to the API key. Pick \"Claude Code "
                                    "(your login)\" in the selector to use your Claude Code "
                                    "subscription instead."),
                     "gray");
            }
            finish(true);
            return;
        }

        const std::string stop = resp.value("stop_reason", "");
        const json       &content = resp["content"];

        if (stop == "refusal") {
            note(QStringLiteral("Claude declined this request."), "#c0392b");
            finish();
            return;
        }

        // The assistant turn goes back VERBATIM — thinking blocks included.
        messages_.push_back({{"role", "assistant"}, {"content", content}});

        bool has_tool_use = false;
        for (const json &block : content) {
            has_tool_use = has_tool_use || block.value("type", "") == "tool_use";
        }
        if (stop == "max_tokens" && has_tool_use) {
            // A truncated turn may carry a cut-off tool input: run nothing, and
            // drop the turn so the history never holds an unanswered tool_use.
            messages_.erase(messages_.end() - 1);
            note(QStringLiteral("The reply hit max_tokens mid tool call; ask again, or more "
                                "narrowly."),
                 "#c0392b");
            finish();
            return;
        }

        json results = json::array();
        for (const json &block : content) {
            const std::string type = block.value("type", "");
            if (type == "text") {
                bubble(QStringLiteral("Claude"), QString::fromStdString(block["text"]), "#8e44ad");
            } else if (type == "tool_use") {
                results.push_back(run_tool(block));
            }
        }
        if (results.empty() || cancelled_) {
            finish();
            return;
        }
        // Every tool_result of this turn, in ONE user message.
        messages_.push_back({{"role", "user"}, {"content", results}});
        request();
    }

    json run_tool(const json &block) {
        const std::string name  = block["name"];
        const json        input = block.value("input", json::object());
        tool_call(QString::fromStdString(name), input);

        scenesync::publish(*interp_, *server_); // the server sees what the scene holds

        json        result;
        bool        is_error = false;
        std::string text;
        try {
            result   = server_->call_tool(name, input);
            is_error = result.value("isError", false);
            text     = result["content"][0].value("text", "");
        } catch (const std::exception &e) { // unknown tool — a protocol-level error
            is_error = true;
            text     = e.what();
        }

        scenesync::adopt(*interp_, *server_); // ...and adopts what the call created or kept
        emit sceneChanged();

        tool_result(QString::fromStdString(text), is_error);
        return {{"type", "tool_result"},
                {"tool_use_id", block["id"]},
                {"content", text},
                {"is_error", is_error}};
    }

    void busy(bool on) {
        busy_ = on;
        input_->setEnabled(!on);
        send_->setEnabled(!on);
        stop_->setEnabled(on);
        backend_->setEnabled(!on);
        status_->setText(!on ? QString()
                             : claude_code() ? QStringLiteral("Starting Claude Code…")
                                             : QStringLiteral("%1 is working…").arg(model_));
    }

    void finish(bool can_retry = false) {
        busy(false);
        // Retry replays the API backend's pending turn; Claude Code keeps its own history.
        retry_->setEnabled(!claude_code() && can_retry && !messages_.empty() &&
                           messages_.back()["role"] == "user");
        input_->setFocus();
        emit turnFinished();
    }

    // ---- the transcript ---------------------------------------------------

    void greet() {
        if (claude_code()) {
            note(QStringLiteral("Chat with Claude about the scene, through Claude Code and your "
                                "Claude Code login (no API key). It sees the objects in the list "
                                "on the left and changes them through this viewer's MCP server "
                                "(%1) — and nothing else: no shell, no file access.")
                     .arg(tool_names()),
                 "gray");
            if (cli_.isEmpty()) {
                note(QStringLiteral("The `claude` CLI was not found on PATH — install Claude Code, "
                                    "or set ROSETTA_CLAUDE_CLI."),
                     "#c0392b");
            }
            return;
        }
        note(QStringLiteral("Chat with Claude about the scene, through the Anthropic API. It sees "
                            "the objects in the list on the left and can change them with the "
                            "same tools an MCP client gets (%1). Model: %2.")
                 .arg(tool_names(), model_),
             "gray");
        if (!has_credentials()) {
            note(QStringLiteral("No API key in the environment, so this backend is off."), "#c0392b");
            no_key_hint();
        }
    }

    // Without a key the scene is still reachable from Claude Code, through the
    // MCP server the viewer hosts — say so where the user is looking.
    void no_key_hint() {
        note(QStringLiteral("Pick \"Claude Code (your login)\" in the selector to chat with your "
                            "Claude Code login instead — no API key needed."),
             "gray");
    }

    QString tool_names() const {
        QStringList n;
        for (const json &t : server_->tools()) {
            n << QString::fromStdString(t["name"]);
        }
        return n.join(QStringLiteral(", "));
    }

    void scroll_to_end() {
        log_->moveCursor(QTextCursor::End);
        log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
    }

    void bubble(const QString &who, const QString &markdown, const char *colour) {
        log_->append(QStringLiteral("<p style='margin-top:10px'><b style='color:%1'>%2</b></p>")
                         .arg(colour, who));
        QTextCursor c = log_->textCursor();
        c.movePosition(QTextCursor::End);
        // A fresh block with DEFAULT formats: otherwise the markdown inherits
        // whatever the previous HTML block carried (a code block's font, a
        // tool result's colour) and can end up unreadable.
        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
        c.insertMarkdown(markdown);
        scroll_to_end();
    }

    void note(const QString &text, const char *colour) {
        log_->append(QStringLiteral("<p style='color:%1'><i>%2</i></p>")
                         .arg(colour, text.toHtmlEscaped()));
        scroll_to_end();
    }

    void tool_call(const QString &name, const json &input) {
        // Scripts are shown as code; everything else as compact JSON.
        QString body;
        if (input.contains("code") && input["code"].is_string()) {
            QStringList lines = QString::fromStdString(input["code"]).split('\n');
            while (!lines.isEmpty() && lines.first().trimmed().isEmpty()) {
                lines.removeFirst();
            }
            if (lines.size() > 25) {
                const int more = lines.size() - 25;
                lines          = lines.mid(0, 25);
                lines << QStringLiteral("… (%1 more lines)").arg(more);
            }
            body = QStringLiteral("<pre style='margin:2px 0 2px 16px'>%1</pre>")
                       .arg(lines.join('\n').toHtmlEscaped());
        } else {
            QString args = QString::fromStdString(input.dump());
            if (args.size() > 200) {
                args = args.left(200) + QStringLiteral("…");
            }
            body = QStringLiteral(" <code>%1</code>").arg(args.toHtmlEscaped());
        }
        log_->append(QStringLiteral("<div style='color:#16a085; margin-left:8px'>▸ <b>%1</b>%2</div>")
                         .arg(name.toHtmlEscaped(), body));
        scroll_to_end();
    }

    void tool_result(QString text, bool is_error) {
        text = text.simplified();
        if (text.size() > 300) {
            text = text.left(300) + QStringLiteral("…");
        }
        log_->append(QStringLiteral("<div style='color:%1; margin-left:24px; font-size:11px'>%2</div>")
                         .arg(is_error ? "#c0392b" : "gray", text.toHtmlEscaped()));
        scroll_to_end();
    }

    static constexpr int k_max_rounds = 40;

    dynui::Interp        *interp_;
    rosetta::mcp::Server *server_;

    QTextBrowser *log_    = nullptr;
    QLineEdit    *input_  = nullptr;
    QPushButton  *send_   = nullptr;
    QPushButton  *stop_   = nullptr;
    QPushButton  *retry_  = nullptr;
    QPushButton  *reset_  = nullptr;
    QLabel       *status_ = nullptr;
    QComboBox    *backend_ = nullptr;

    QNetworkAccessManager net_;
    QNetworkReply        *reply_ = nullptr;
    QString               model_, base_;
    json                  messages_ = json::array();
    int                   rounds_   = 0;
    bool                  busy_ = false, cancelled_ = false;

    // Claude Code backend
    QString    cli_, mcp_url_, session_id_;
    QProcess  *proc_ = nullptr;
    QByteArray out_buf_;
    bool       got_result_ = false;
};
