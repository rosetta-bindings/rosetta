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
// OLLAMA (local) — a model running on this machine through Ollama, with no
// account, key or network: the panel calls Ollama's native /api/chat (which,
// unlike its Anthropic-compatible endpoint, lets a request set the context size
// and turn thinking off) and runs the same in-process tool loop. Only models
// Ollama reports as tool-capable are offered. A small model does not explore
// the way Claude does, so its system prompt carries a reference of the bound
// library — every class, field (type, range, choices) and method, built from
// the same registry the property panel reads — plus worked tool-call examples
// and the objects in the scene; obvious argument-name slips (`target` for
// `object`) are forgiven.
//
// Configuration (environment):
//   ANTHROPIC_API_KEY      API backend (or ANTHROPIC_AUTH_TOKEN, sent as Bearer)
//   ANTHROPIC_BASE_URL     API backend, default https://api.anthropic.com
//   ROSETTA_CLAUDE_MODEL   model; default claude-opus-5 (API), Claude Code's own
//                          default (Claude Code)
//   ROSETTA_CLAUDE_CLI     path to the `claude` executable (default: found on PATH)
//   OLLAMA_HOST            Ollama backend, default http://localhost:11434
//   ROSETTA_OLLAMA_MODEL   Ollama backend: model to preselect (default: the last
//                          one used, else qwen3:8b when installed)
//   ROSETTA_ASSISTANT      claude-code | api | ollama: the backend for this run
//                          (default: the one last picked in the panel)
//   ROSETTA_OLLAMA_CTX     Ollama backend: context window, default 16384 tokens
//                          (Ollama's own default, 4096, cannot hold the tools)
//
// VOICE (when built with whisper.cpp, see voice.h): the microphone button (or
// F8) records, a second click transcribes locally, and the text lands in the
// prompt box to be checked and sent — or is sent at once with "send when
// done". Send (or Enter) while recording stops the microphone and sends the
// transcript as soon as it is ready. Recognition is biased towards the scene's own vocabulary: the bound
// class, method and field names, and the objects in the list.

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QStandardPaths>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include <rosetta/runtime/mcp.h>

#include <map>
#include <memory>
#include <utility>

#include "../interp.h"
#include "scenesync.h"

#ifdef ROSETTA_VOICE
#include "voice.h"
#include <set>
#endif

/**
 * @brief The prompt box: multi-line, Enter sends, Shift+Enter starts a new line.
 *
 * Grows with its content up to a few lines, then scrolls — so a long request
 * (or a pasted script) is readable before it is sent.
 */
class PromptEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit PromptEdit(QWidget *parent = nullptr) : QPlainTextEdit(parent) {
        setTabChangesFocus(true);
        connect(this, &QPlainTextEdit::textChanged, this, &PromptEdit::fit);
        fit();
    }

signals:
    void submitted();

protected:
    void keyPressEvent(QKeyEvent *e) override {
        const bool enter = e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter;
        if (enter && !(e->modifiers() & Qt::ShiftModifier)) {
            emit submitted();
            return;
        }
        if (enter) { // Shift+Enter: a plain newline, never a new paragraph block style
            insertPlainText(QStringLiteral("\n"));
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
    }

private:
    void fit() {
        const int lines  = qBound(1, int(document()->blockCount()), 6);
        const int line_h = fontMetrics().lineSpacing();
        const int frame  = int(document()->documentMargin() * 2) + frameWidth() * 2 + 4;
        setFixedHeight(qMax(2, lines) * line_h + frame);
    }
};

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

        input_ = new PromptEdit(this);
        input_->setPlaceholderText(
            QStringLiteral("Ask Claude — e.g. \"relax the bunny until its worst triangle "
                           "is above 0.1, then make it glossy\"   (Enter sends, "
                           "Shift+Enter: new line)"));
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
        backend_->addItem(QStringLiteral("Ollama (local)"));
        backend_->setToolTip(QStringLiteral(
            "Claude Code: runs the `claude` CLI with your Claude Code login, connected to this "
            "viewer's MCP server.\nAnthropic API: calls the API directly with ANTHROPIC_API_KEY."
            "\nOllama: a model running on this machine — no account, no key, nothing leaves "
            "it."));

        ollama_model_ = new QComboBox(this);
        ollama_model_->setToolTip(QStringLiteral(
            "Ollama models that support tool calling (`ollama pull qwen3:8b` adds one)."));
        think_ = new QCheckBox(QStringLiteral("think"), this);
        think_->setToolTip(QStringLiteral(
            "Let the model reason before answering: sometimes better choices, always much slower."));

        auto *row = new QHBoxLayout;
        row->addWidget(backend_);
        row->addWidget(ollama_model_);
        row->addWidget(think_);
        row->addWidget(input_, 1);
#ifdef ROSETTA_VOICE
        setup_voice(row);
#endif
        row->addWidget(send_);
        row->addWidget(stop_);
        row->addWidget(retry_);
        row->addWidget(reset_);

        v->addWidget(log_, 1);
        v->addLayout(row);
        v->addWidget(status_);

        connect(input_, &PromptEdit::submitted, this, &ClaudePanel::submit);
        connect(send_, &QPushButton::clicked, this, &ClaudePanel::submit);
        connect(stop_, &QPushButton::clicked, this, &ClaudePanel::stop);
        connect(retry_, &QPushButton::clicked, this, [this] {
            retry_->setEnabled(false);
            ollama() ? ollama_request() : request();
        });
        connect(reset_, &QPushButton::clicked, this, [this] {
            stop();
            messages_        = json::array();
            ollama_messages_ = json::array();
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

        ollama_base_ = ollama_base_url();
        ollama_ctx_  = qEnvironmentVariableIsSet("ROSETTA_OLLAMA_CTX")
                           ? qEnvironmentVariableIntValue("ROSETTA_OLLAMA_CTX")
                           : 16384;

        // Default: the backend used last time; else Claude Code when it is
        // installed (no key needed), else the API.
        const int last = QSettings(QStringLiteral("rosetta"), QStringLiteral("scene-viewer"))
                             .value(QStringLiteral("assistant/backend"), -1)
                             .toInt();
        backend_->setCurrentIndex(last >= 0 && last < backend_->count()
                                      ? last
                                      : (!cli_.isEmpty() || !has_credentials() ? 0 : 1));
        // ROSETTA_ASSISTANT picks one for this run only (scripted runs, --claude).
        const QString forced = qEnvironmentVariable("ROSETTA_ASSISTANT").toLower();
        if (forced == QStringLiteral("claude-code") || forced == QStringLiteral("api") ||
            forced == QStringLiteral("ollama")) {
            backend_->setCurrentIndex(forced == QStringLiteral("claude-code") ? 0
                                      : forced == QStringLiteral("api")   ? 1
                                                                          : 2);
        }
        // Connected after the initial pick, so only a choice made in the UI is remembered.
        connect(backend_, &QComboBox::currentIndexChanged, this, [this](int i) {
            QSettings(QStringLiteral("rosetta"), QStringLiteral("scene-viewer"))
                .setValue(QStringLiteral("assistant/backend"), i);
            backend_changed();
        });
        connect(ollama_model_, &QComboBox::currentTextChanged, this, [this](const QString &m) {
            if (!m.isEmpty() && !filling_models_) {
                QSettings(QStringLiteral("rosetta"), QStringLiteral("scene-viewer"))
                    .setValue(QStringLiteral("assistant/ollama_model"), m);
            }
        });
        backend_changed();
    }

    /** @brief The viewer's MCP endpoint, for the Claude Code backend. */
    void set_mcp_url(const QString &url) { mcp_url_ = url; }

    /** @brief Send `text` as if typed (used by the viewer's --claude flag). */
    void ask(const QString &text) {
        input_->setPlainText(text);
        if (ollama() && ollama_model_->count() == 0) {
            ask_when_ready_ = true; // the model list is still loading
            return;
        }
        submit();
    }

signals:
    /** @brief A tool call may have changed the scene: re-read it. */
    void sceneChanged();

    /** @brief A message has been fully answered (or failed, or was stopped). */
    void turnFinished();

private slots:
    void submit() {
#ifdef ROSETTA_VOICE
        // Send while speaking = stop the microphone, then send what was said
        // (with anything already typed) once it is transcribed. Also covers a
        // Send pressed while an earlier recording is still being transcribed.
        if (voice_->recording() || voice_->transcribing()) {
            send_on_transcript_ = true;
            voice_->stop(); // no-op while transcribing
            return;
        }
#endif
        const QString text = input_->toPlainText().trimmed();
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
        if (ollama()) {
            if (ollama_model_->currentText().isEmpty()) {
                note(QStringLiteral("No tool-capable Ollama model is available (see above)."),
                     "#c0392b");
                return;
            }
            input_->clear();
            bubble(QStringLiteral("You"), text, "#2e86c1");
            ollama_messages_.push_back({{"role", "user"}, {"content", text.toStdString()}});
            rounds_ = 0;
            ollama_request();
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
#ifdef ROSETTA_VOICE
        voice_->cancel();
#endif
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
    bool ollama() const { return backend_->currentIndex() == 2; }

    void backend_changed() {
        ollama_model_->setVisible(ollama());
        think_->setVisible(ollama());
        input_->setPlaceholderText(
            ollama() ? QStringLiteral("Ask the local model — e.g. \"decimate the bunny to 10%, "
                                      "then make it glossy\"   (Enter sends, Shift+Enter: new "
                                      "line)")
                     : QStringLiteral("Ask Claude — e.g. \"relax the bunny until its worst "
                                      "triangle is above 0.1, then make it glossy\"   (Enter "
                                      "sends, Shift+Enter: new line)"));
        if (busy_) {
            return;
        }
        greet();
        if (ollama()) {
            refresh_ollama_models();
        }
    }

#ifdef ROSETTA_VOICE
    // ---- voice input --------------------------------------------------------

    void setup_voice(QHBoxLayout *row) {
        voice_ = new voice::VoiceInput(this);

        mic_ = new QToolButton(this);
        mic_->setText(QStringLiteral("🎤"));
        mic_->setCheckable(true);
        mic_->setToolTip(QStringLiteral(
            "Speak instead of typing (F8): click, talk, click again — or press Enter (or Send) "
            "to stop and send at once.\n"
            "Transcribed on this machine by whisper.cpp; Esc cancels."));
        auto_send_ = new QCheckBox(QStringLiteral("send when done"), this);
        auto_send_->setToolTip(QStringLiteral(
            "Send the transcript to Claude at once, instead of leaving it in the prompt box to "
            "check first."));
        row->addWidget(mic_);
        row->addWidget(auto_send_);

        auto *toggle = new QShortcut(QKeySequence(Qt::Key_F8), this);
        toggle->setContext(Qt::WindowShortcut);
        auto *cancel = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        cancel->setContext(Qt::WindowShortcut); // like F8: works with the focus anywhere

        tick_ = new QTimer(this);
        tick_->setInterval(200);

        connect(mic_, &QToolButton::clicked, this, &ClaudePanel::toggle_voice);
        connect(toggle, &QShortcut::activated, this, &ClaudePanel::toggle_voice);
        connect(cancel, &QShortcut::activated, voice_, &voice::VoiceInput::cancel);
        connect(tick_, &QTimer::timeout, this, [this] {
            status_->setText(QStringLiteral("● Listening… %1 s — Enter (or Send) to stop and send, "
                                            "🎤 / F8 to stop, Esc to cancel")
                                 .arg(voice_->seconds(), 0, 'f', 1));
        });
        cancel->setEnabled(false); // Esc belongs to the rest of the window unless recording

        // Enter = stop and send, wherever the focus is. The prompt box already
        // sends on Enter, but after clicking 🎤 (or pressing F8 from the 3D
        // view) the focus is elsewhere. Only live while speaking or
        // transcribing, so the console's own Enter is untouched otherwise.
        QShortcut *send_keys[] = {new QShortcut(QKeySequence(Qt::Key_Return), this),
                                  new QShortcut(QKeySequence(Qt::Key_Enter), this)}; // keypad
        for (QShortcut *k : send_keys) {
            k->setContext(Qt::WindowShortcut);
            k->setEnabled(false);
            connect(k, &QShortcut::activated, this, &ClaudePanel::submit);
        }
        const auto arm_send_keys = [this, send_keys] {
            const bool live = voice_->recording() || voice_->transcribing();
            for (QShortcut *k : send_keys) {
                k->setEnabled(live);
            }
        };

        connect(voice_, &voice::VoiceInput::recordingChanged, this, [this, cancel, arm_send_keys](bool on) {
            arm_send_keys();
            cancel->setEnabled(on);
            mic_->setChecked(on);
            mic_->setStyleSheet(on ? QStringLiteral("background: #c0392b; color: white;") : QString());
            on ? tick_->start() : tick_->stop();
            if (!on) {
                status_->clear();
            }
        });
        connect(voice_, &voice::VoiceInput::transcribingChanged, this, [this, arm_send_keys](bool on) {
            arm_send_keys();
            mic_->setEnabled(!on && !busy_);
            status_->setText(!on                  ? QString()
                             : send_on_transcript_ ? QStringLiteral("Transcribing… (then sending)")
                                                   : QStringLiteral("Transcribing…"));
        });
        connect(voice_, &voice::VoiceInput::transcribed, this, [this](const QString &text) {
            QString prompt = input_->toPlainText().trimmed();
            prompt += (prompt.isEmpty() ? QString() : QStringLiteral(" ")) + text;
            input_->setPlainText(prompt);
            input_->moveCursor(QTextCursor::End);
            input_->setFocus();
            const bool send     = auto_send_->isChecked() || send_on_transcript_;
            send_on_transcript_ = false;
            if (send) {
                submit();
            }
        });
        connect(voice_, &voice::VoiceInput::failed, this, [this](const QString &why) {
            mic_->setChecked(false);
            note(QStringLiteral("Voice: %1").arg(why), "#c0392b");
            // Send was pressed: nothing was heard, but anything typed still goes.
            const bool send     = send_on_transcript_;
            send_on_transcript_ = false;
            if (send) {
                submit();
            }
        });

        if (!voice_->model_available()) {
            mic_->setToolTip(mic_->toolTip() +
                             QStringLiteral("\n\nNo model yet — run `cmake -P "
                                            "fetch_whisper_model.cmake` in examples/dynamic, or "
                                            "set ROSETTA_WHISPER_MODEL."));
        }
    }

    void toggle_voice() {
        if (voice_->recording()) {
            voice_->stop();
            return;
        }
        if (busy_ || voice_->transcribing()) {
            mic_->setChecked(false);
            return;
        }
        voice_->set_vocabulary(vocabulary());
        voice_->start();
    }

    // What whisper should expect to hear: the objects in the scene first, then
    // the bound classes, then their members.
    QStringList vocabulary() const {
        QStringList          out;
        std::set<std::string> seen;
        const auto add = [&](const std::string &w) {
            if (!w.empty() && seen.insert(w).second) {
                out << QString::fromStdString(w);
            }
        };
        for (const auto &[name, obj] : interp_->vars) {
            add(name);
        }
        const auto &classes = server_->registry().classes();
        for (const auto *k : classes) {
            add(k->name);
        }
        for (const auto *k : classes) {
            for (std::size_t i = 0; i < k->n_methods; ++i) {
                add(k->methods[i].name);
            }
            for (std::size_t i = 0; i < k->n_fields; ++i) {
                add(k->fields[i].name);
            }
        }
        return out;
    }
#endif

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
        const auto [text, is_error] = execute_tool(name, input);
        return {{"type", "tool_result"},
                {"tool_use_id", block["id"]},
                {"content", text},
                {"is_error", is_error}};
    }

    /** @brief Run one tool against the live scene, and log it. Returns (text, is_error). */
    std::pair<std::string, bool> execute_tool(const std::string &name, const json &input) {
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
        return {text, is_error};
    }

    void busy(bool on) {
        busy_ = on;
        input_->setEnabled(!on);
        send_->setEnabled(!on);
        stop_->setEnabled(on);
        backend_->setEnabled(!on);
        ollama_model_->setEnabled(!on);
        think_->setEnabled(!on);
#ifdef ROSETTA_VOICE
        mic_->setEnabled(!on && !voice_->transcribing());
#endif
        status_->setText(!on            ? QString()
                         : claude_code() ? QStringLiteral("Starting Claude Code…")
                         : ollama()      ? QStringLiteral("%1 (Ollama, on this machine) is working…")
                                          .arg(ollama_model_->currentText())
                                         : QStringLiteral("%1 is working…").arg(model_));
    }

    void finish(bool can_retry = false) {
        busy(false);
        // Retry replays the API backend's pending turn; Claude Code keeps its own history.
        const json &history = ollama() ? ollama_messages_ : messages_;
        retry_->setEnabled(!claude_code() && can_retry && !history.empty() &&
                           history.back()["role"] != "assistant");
        input_->setFocus();
        emit turnFinished();
    }

    // ---- the Ollama backend --------------------------------------------------

    static QString ollama_base_url() {
        QString h = qEnvironmentVariable("OLLAMA_HOST", QStringLiteral("http://localhost:11434"));
        if (!h.contains(QStringLiteral("://"))) {
            h.prepend(QStringLiteral("http://"));
        }
        QUrl u(h);
        if (u.host() == QStringLiteral("0.0.0.0")) { // a bind address, not one to call
            u.setHost(QStringLiteral("127.0.0.1"));
        }
        if (u.port() < 0) {
            u.setPort(11434);
        }
        return u.toString(QUrl::StripTrailingSlash);
    }

    QNetworkRequest ollama_req(const QString &path) const {
        QNetworkRequest req(QUrl(ollama_base_ + path));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        return req;
    }

    // The installed models that can call tools: /api/tags lists them,
    // /api/show says which have the "tools" capability.
    void refresh_ollama_models() {
        QNetworkReply *r = net_.get(ollama_req(QStringLiteral("/api/tags")));
        connect(r, &QNetworkReply::finished, this, [this, r] {
            r->deleteLater();
            const json tags = json::parse(r->readAll().toStdString(), nullptr, false);
            if (r->error() != QNetworkReply::NoError || tags.is_discarded()) {
                note(QStringLiteral("Ollama is not reachable at %1 — start it (the Ollama app, or "
                                    "`ollama serve`), or set OLLAMA_HOST.")
                         .arg(ollama_base_),
                     "#c0392b");
                return;
            }
            std::vector<std::string> names;
            for (const json &m : tags.value("models", json::array())) {
                names.push_back(m.value("name", ""));
            }
            if (names.empty()) {
                note(QStringLiteral("Ollama has no models yet: `ollama pull qwen3:8b`."), "#c0392b");
                return;
            }
            auto pending = std::make_shared<std::size_t>(names.size());
            auto usable  = std::make_shared<std::map<std::string, bool>>();
            for (const std::string &name : names) {
                QNetworkReply *s = net_.post(ollama_req(QStringLiteral("/api/show")),
                                             QByteArray::fromStdString(json{{"model", name}}.dump()));
                connect(s, &QNetworkReply::finished, this, [this, s, name, pending, usable] {
                    s->deleteLater();
                    const json info  = json::parse(s->readAll().toStdString(), nullptr, false);
                    bool       tools = false;
                    if (!info.is_discarded()) {
                        for (const json &c : info.value("capabilities", json::array())) {
                            tools = tools || c == "tools";
                        }
                    }
                    (*usable)[name] = tools;
                    if (--*pending == 0) {
                        fill_ollama_models(*usable);
                    }
                });
            }
        });
    }

    void fill_ollama_models(const std::map<std::string, bool> &usable) {
        const QString want = qEnvironmentVariable(
            "ROSETTA_OLLAMA_MODEL", QSettings(QStringLiteral("rosetta"), QStringLiteral("scene-viewer"))
                                        .value(QStringLiteral("assistant/ollama_model"))
                                        .toString());
        filling_models_ = true;
        ollama_model_->clear();
        QStringList skipped;
        for (const auto &[name, tools] : usable) {
            if (tools) {
                ollama_model_->addItem(QString::fromStdString(name));
            } else {
                skipped << QString::fromStdString(name);
            }
        }
        int pick = ollama_model_->findText(want);
        if (pick < 0) {
            pick = ollama_model_->findText(QStringLiteral("qwen3:8b"));
        }
        ollama_model_->setCurrentIndex(pick < 0 ? 0 : pick);
        filling_models_ = false;
        if (ask_when_ready_ && ollama_model_->count() > 0) {
            ask_when_ready_ = false;
            submit();
        }
        if (!skipped.isEmpty()) {
            note(QStringLiteral("Not offered (no tool calling in Ollama): %1.")
                     .arg(skipped.join(QStringLiteral(", "))),
                 "gray");
        }
        if (ollama_model_->count() == 0) {
            note(QStringLiteral("None of the installed models can call tools — `ollama pull "
                                "qwen3:8b` adds one that can."),
                 "#c0392b");
        }
    }

    json ollama_tools() const {
        json out = json::array();
        for (const json &t : server_->tools()) {
            out.push_back({{"type", "function"},
                           {"function",
                            {{"name", t["name"]},
                             {"description", t["description"]},
                             {"parameters", t["inputSchema"]}}}});
        }
        return out;
    }

    // What a small model needs up front. The rules and the library reference
    // come first and never change, so Ollama can reuse their cached prefix
    // from one round to the next; the scene's objects change, so they go last.
    std::string ollama_system_prompt() const {
        std::string out =
            "You control a live 3D viewer of a C++ scene library through tools. The user sees "
            "the 3D view, an object list and a property panel; every change you make is shown "
            "immediately.\n\n"
            "Rules:\n"
            "- Act with the tools; do not describe code or tell the user how to do it.\n"
            "- Use ONLY the classes, fields and methods in the reference below, with their exact "
            "names. Values must respect the ranges and choices given.\n"
            "- Objects in the scene are passed by name (e.g. \"bunny\").\n"
            "- To use a solver (a class whose run method takes a Mesh): create it, set its "
            "fields, then call run with the mesh.\n"
            "- A no-argument method that runs a solver with its defaults (its description says "
            "so) is only for a request with no amount. When the user gives an amount or a "
            "setting (\"to 10%\", \"20 passes\"), use the solver class and set the field that "
            "controls it.\n"
            "- Check every result against the request. If the numbers do not match it, fix it "
            "with another call, or say plainly that it did not work - never claim success the "
            "results do not show.\n"
            "- When done, reply in one or two short sentences with what changed and the numbers "
            "that matter.\n\n"
            "Examples of correct tool calls:\n"
            "  call_method {\"target\": \"bunny\", \"method\": \"subdivide\"}\n"
            "  set_fields {\"object\": \"bunny\", \"values\": {\"preset\": \"glossy\", "
            "\"opacity\": 0.5}}\n"
            "  create_object {\"class\": \"Decimator\", \"name\": \"dec\"}\n"
            "  set_fields {\"object\": \"dec\", \"values\": {\"ratio\": 0.1}}\n"
            "  call_method {\"target\": \"dec\", \"method\": \"run\", \"args\": [\"bunny\"]}\n"
            "  call_method {\"target\": \"Relaxer\", \"method\": \"minQuality\", \"args\": "
            "[\"bunny\"]}   (a static method: the target is the class)\n\n";
        out += library_reference();
        out += "\nObjects in the scene now:";
        for (const auto &[name, obj] : interp_->vars) {
            out += "\n  " + name + " (" + std::string(obj.meta() ? obj.meta()->name : "?") + ")";
        }
        out += "\n";
        return out;
    }

    static std::string first_line(const char *doc, std::size_t max = 160) {
        std::string s = doc ? doc : "";
        s             = s.substr(0, s.find('\n'));
        return s.size() > max ? s.substr(0, max) + "..." : s;
    }

    static std::string type_text(const rosetta::dyn::TypeDesc *t) {
        if (!t) {
            return "?";
        }
        std::string s = t->spelling;
        if (t->kind == rosetta::dyn::Kind::enum_ && t->n_enumerators) {
            s += " (one of:";
            for (std::size_t i = 0; i < t->n_enumerators; ++i) {
                s += std::string(i ? ", " : " ") + t->enumerators[i].name;
            }
            s += ")";
        }
        return s;
    }

    // Every bound class: what it is, its fields with their constraints, and
    // its callable methods with their parameters — read from the registry,
    // so it is always the library as built.
    std::string library_reference() const {
        std::string out = "Library reference:\n";
        for (const auto *k : server_->registry().classes()) {
            out += "\nclass " + std::string(k->name);
            if (const std::string d = first_line(k->doc); !d.empty()) {
                out += " - " + d;
            }
            out += "\n";
            for (std::size_t i = 0; i < k->n_fields; ++i) {
                const auto &f = k->fields[i];
                out += "  field " + std::string(f.name) + ": " + type_text(f.type);
                if (f.range.has) {
                    out += " in [" + json(f.range.lo).dump() + ", " + json(f.range.hi).dump() + "]";
                }
                if (f.n_choices) {
                    out += ", one of:";
                    for (std::size_t c = 0; c < f.n_choices; ++c) {
                        out += std::string(c ? ", " : " ") + "\"" + f.choices[c] + "\"";
                    }
                }
                if (f.readonly) {
                    out += ", read-only";
                }
                if (const std::string d = first_line(f.doc, 100); !d.empty()) {
                    out += " - " + d;
                }
                out += "\n";
            }
            for (std::size_t i = 0; i < k->n_methods; ++i) {
                const auto &m = k->methods[i];
                if (!m.invoke) {
                    continue; // described but not callable: not worth the model's attention
                }
                out += std::string("  ") + (m.is_static ? "static " : "") + "method " + m.name + "(";
                for (std::size_t a = 0; a < m.n_params; ++a) {
                    out += std::string(a ? ", " : "") + m.params[a].name + ": " +
                           type_text(m.params[a].type);
                }
                out += ")";
                if (m.ret && m.ret->kind != rosetta::dyn::Kind::void_) {
                    out += " -> " + type_text(m.ret);
                }
                if (const std::string d = first_line(m.doc, 100); !d.empty()) {
                    out += " - " + d;
                }
                out += "\n";
            }
        }
        return out;
    }

    // Small models mix up the tools' argument names (`target` vs `object`,
    // `fields` vs `values`); take the obvious intent rather than fail.
    static json normalize_args(const std::string &tool, json a) {
        if (a.is_string()) { // some models send the arguments as a JSON string
            a = json::parse(a.get<std::string>(), nullptr, false);
        }
        if (!a.is_object()) {
            return json::object();
        }
        const auto alias = [&](const char *to, std::initializer_list<const char *> from) {
            for (const char *f : from) {
                if (!a.contains(to) && a.contains(f)) {
                    a[to] = a[f];
                    a.erase(f);
                }
            }
        };
        if (tool == "call_method") {
            alias("target", {"object", "name", "class"});
            alias("args", {"arguments", "params"});
        } else if (tool == "set_fields") {
            alias("object", {"target", "name"});
            alias("values", {"fields"});
        } else if (tool == "inspect_object" || tool == "release_object") {
            alias("object", {"target", "name"});
        } else if (tool == "create_object") {
            alias("class", {"class_name", "type"});
        } else if (tool == "describe_class") {
            alias("class", {"name", "class_name"});
        }
        return a;
    }

    void ollama_request() {
        if (++rounds_ > k_max_rounds) {
            note(QStringLiteral("Stopped after %1 tool rounds.").arg(k_max_rounds), "#c0392b");
            finish();
            return;
        }
        busy(true);
        cancelled_ = false;

        json msgs = json::array({{{"role", "system"}, {"content", ollama_system_prompt()}}});
        for (const json &m : ollama_messages_) {
            msgs.push_back(m);
        }
        const json body = {
            {"model", ollama_model_->currentText().toStdString()},
            {"stream", false},
            {"think", think_->isChecked()},
            {"keep_alive", "30m"},
            {"options", {{"num_ctx", ollama_ctx_}}},
            {"messages", msgs},
            {"tools", ollama_tools()},
        };
        QNetworkRequest req = ollama_req(QStringLiteral("/api/chat"));
        req.setTransferTimeout(15 * 60 * 1000); // a first load plus a long think
        reply_ = net_.post(req, QByteArray::fromStdString(body.dump()));
        connect(reply_, &QNetworkReply::finished, this, [this, r = reply_] { on_ollama_reply(r); });
    }

    void on_ollama_reply(QNetworkReply *r) {
        r->deleteLater();
        reply_ = nullptr;
        if (cancelled_) {
            note(QStringLiteral("Stopped."), "gray");
            finish(true);
            return;
        }
        const json resp = json::parse(r->readAll().toStdString(), nullptr, false);
        if (r->error() != QNetworkReply::NoError || resp.is_discarded() || resp.contains("error")) {
            QString why = r->errorString();
            if (!resp.is_discarded() && resp.contains("error")) {
                why = QString::fromStdString(resp["error"].is_string()
                                                 ? resp["error"].get<std::string>()
                                                 : resp["error"].dump());
            }
            if (r->error() == QNetworkReply::ConnectionRefusedError) {
                why = QStringLiteral("not running at %1").arg(ollama_base_);
            }
            note(QStringLiteral("Ollama: %1").arg(why), "#c0392b");
            finish(true);
            return;
        }

        const json &msg     = resp.contains("message") ? resp["message"] : json::object();
        std::string content = msg.value("content", "");
        // Some models inline their reasoning instead of returning it apart.
        for (std::size_t a; (a = content.find("<think>")) != std::string::npos;) {
            const std::size_t b = content.find("</think>", a);
            content.erase(a, b == std::string::npos ? std::string::npos : b + 8 - a);
        }
        const json calls = msg.value("tool_calls", json::array());

        json assistant = {{"role", "assistant"}, {"content", content}};
        if (!calls.empty()) {
            assistant["tool_calls"] = calls;
        }
        ollama_messages_.push_back(assistant);

        const QString text = QString::fromStdString(content).trimmed();
        if (!text.isEmpty()) {
            bubble(ollama_model_->currentText(), text, "#8e44ad");
        }
        if (calls.empty()) {
            if (text.isEmpty()) {
                // Nothing to keep: drop the empty turn so Retry re-asks cleanly.
                ollama_messages_.erase(ollama_messages_.end() - 1);
                note(QStringLiteral("The model returned an empty reply — Retry, rephrase, or tick "
                                    "\"think\"."),
                     "#c0392b");
                finish(true);
                return;
            }
            finish();
            return;
        }
        for (const json &c : calls) {
            const json        fn   = c.value("function", json::object());
            const std::string name = fn.value("name", "");
            const auto [result, is_error] =
                execute_tool(name, normalize_args(name, fn.value("arguments", json::object())));
            (void)is_error; // the text says so; Ollama's tool message has no error flag
            ollama_messages_.push_back({{"role", "tool"}, {"tool_name", name}, {"content", result}});
        }
        if (cancelled_) {
            note(QStringLiteral("Stopped."), "gray");
            finish(true);
            return;
        }
        ollama_request();
    }

    // ---- the transcript ---------------------------------------------------

    void greet() {
        if (ollama()) {
            note(QStringLiteral("Chat about the scene with a model running on this machine, through "
                                "Ollama (%1) — no account, no key, nothing leaves it. It sees the "
                                "objects in the list on the left and changes them with the same "
                                "tools an MCP client gets; its prompt also carries a reference of "
                                "the bound library, which small models need. Context: %2 tokens.")
                     .arg(ollama_base_)
                     .arg(ollama_ctx_),
                 "gray");
            return;
        }
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
    PromptEdit   *input_  = nullptr;
    QPushButton  *send_   = nullptr;
    QPushButton  *stop_   = nullptr;
    QPushButton  *retry_  = nullptr;
    QPushButton  *reset_  = nullptr;
    QLabel       *status_ = nullptr;
    QComboBox    *backend_ = nullptr;

    // Ollama backend
    QComboBox *ollama_model_    = nullptr;
    QCheckBox *think_           = nullptr;
    QString    ollama_base_;
    int        ollama_ctx_      = 16384;
    json       ollama_messages_ = json::array(); // native /api/chat history, no system prompt
    bool       filling_models_  = false;
    bool       ask_when_ready_  = false; // ask() arrived before the model list

#ifdef ROSETTA_VOICE
    voice::VoiceInput *voice_     = nullptr;
    bool               send_on_transcript_ = false; // Send was pressed while speaking
    QToolButton       *mic_       = nullptr;
    QCheckBox         *auto_send_ = nullptr;
    QTimer            *tick_      = nullptr;
#endif

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
