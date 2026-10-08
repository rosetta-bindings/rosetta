// Copyright (c) fmaerten@gmail.com
// License: MIT

// MCP over HTTP, hosted INSIDE a Qt application.
//
// The stdio server (Server::serve_stdio) is a process an MCP client launches —
// it owns its own objects, so nothing it does is visible in an application
// that is already running. This transport turns it around: the application
// keeps running, listens on 127.0.0.1, and an MCP client (Claude Code,
// Claude Desktop, ...) connects to it. The agent then works on the objects the
// application is showing.
//
//     claude mcp add --transport http scene-viewer http://127.0.0.1:8770/mcp
//
// It speaks the Streamable HTTP transport in its simplest legal form: every
// JSON-RPC message is POSTed to one endpoint and answered with a single
// application/json body (202 Accepted, empty, for a notification). The
// server never initiates messages, so the optional GET event stream is
// declined with 405, as the transport permits.
//
// Built on QTcpServer and therefore on the Qt event loop: requests are handled
// on the thread that owns the host — the GUI thread in an application — so a
// tool call runs between two frames and needs no locking against the code
// that draws or edits the same objects.
//
// Security. Binds to the loopback interface only, and rejects a request whose
// Origin header names anything other than localhost (a web page cannot drive
// the server through DNS rebinding). There is no authentication: any local
// process can connect, exactly as any local process could run the stdio
// binary. Do not change the bind address without adding some.
//
// Header-only, no Q_OBJECT (no moc step for the includer). Needs Qt6::Network.

#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <rosetta/runtime/mcp.h>

#include <functional>
#include <map>
#include <optional>

namespace rosetta::mcp {

    class QtHttpHost {
    public:
        /** @brief Turns one JSON-RPC message into its reply (nullopt for a
         *  notification). Defaults to Server::handle; wrap it to run code
         *  around each call (refresh a view, sync a handle table). */
        using Handler = std::function<std::optional<json>(const json &)>;

        explicit QtHttpHost(Server &server)
            : handler_([&server](const json &m) { return server.handle(m); }) {}

        void set_handler(Handler h) { handler_ = std::move(h); }

        /** @brief Start listening on 127.0.0.1:`port`; false (see error()) on failure. */
        bool listen(quint16 port) {
            QObject::connect(&tcp_, &QTcpServer::newConnection, &tcp_, [this] { accept(); });
            return tcp_.listen(QHostAddress::LocalHost, port);
        }

        QString error() const { return tcp_.errorString(); }
        quint16 port() const { return tcp_.serverPort(); }
        QString url() const { return QStringLiteral("http://127.0.0.1:%1/mcp").arg(port()); }

    private:
        struct Request {
            QByteArray                     method, path, body;
            std::map<QByteArray, QByteArray> headers; // lower-cased names
        };

        void accept() {
            while (QTcpSocket *s = tcp_.nextPendingConnection()) {
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s] { on_data(s); });
                QObject::connect(s, &QTcpSocket::disconnected, s, [this, s] {
                    buffers_.erase(s);
                    s->deleteLater();
                });
            }
        }

        void on_data(QTcpSocket *s) {
            QByteArray &buf = buffers_[s];
            buf += s->readAll();
            // Keep-alive: one read may complete several requests, or none.
            while (true) {
                Request r;
                const int used = parse(buf, r);
                if (used == 0) {
                    return; // incomplete — wait for more bytes
                }
                if (used < 0) {
                    reply(s, 400, "text/plain", "malformed HTTP request", true);
                    return;
                }
                buf.remove(0, used);
                const bool close = header(r, "connection").toLower() == "close";
                respond(s, r, close);
                if (close) {
                    return;
                }
            }
        }

        /** @brief Bytes consumed by one complete request, 0 if incomplete, -1 if malformed. */
        static int parse(const QByteArray &buf, Request &r) {
            const int end = buf.indexOf("\r\n\r\n");
            if (end < 0) {
                return buf.size() > 64 * 1024 ? -1 : 0;
            }
            const QList<QByteArray> lines = buf.left(end).split('\n');
            const QList<QByteArray> start = lines.value(0).trimmed().split(' ');
            if (start.size() < 2) {
                return -1;
            }
            r.method = start[0];
            r.path   = start[1];
            for (int i = 1; i < lines.size(); ++i) {
                const int colon = lines[i].indexOf(':');
                if (colon > 0) {
                    r.headers[lines[i].left(colon).trimmed().toLower()] =
                        lines[i].mid(colon + 1).trimmed();
                }
            }
            bool       ok  = true;
            const auto len = header(r, "content-length");
            const int  n   = len.isEmpty() ? 0 : len.toInt(&ok);
            if (!ok || n < 0 || n > 64 * 1024 * 1024) {
                return -1;
            }
            const int total = end + 4 + n;
            if (buf.size() < total) {
                return 0;
            }
            r.body = buf.mid(end + 4, n);
            return total;
        }

        static QByteArray header(const Request &r, const char *name) {
            auto it = r.headers.find(name);
            return it == r.headers.end() ? QByteArray() : it->second;
        }

        static bool local_origin(const QByteArray &origin) {
            if (origin.isEmpty()) {
                return true; // not a browser
            }
            for (const char *ok : {"http://localhost", "http://127.0.0.1", "https://localhost",
                                   "https://127.0.0.1"}) {
                if (origin == ok || origin.startsWith(QByteArray(ok) + ':')) {
                    return true;
                }
            }
            return false;
        }

        void respond(QTcpSocket *s, const Request &r, bool close) {
            const QByteArray path = r.path.split('?').value(0);
            if (path != "/mcp" && path != "/") {
                reply(s, 404, "text/plain", "MCP endpoint is /mcp", close);
                return;
            }
            if (!local_origin(header(r, "origin"))) {
                reply(s, 403, "text/plain", "cross-origin requests are refused", close);
                return;
            }
            if (r.method != "POST") {
                // No server-initiated stream (GET) and no sessions to end (DELETE).
                reply(s, 405, "text/plain", "POST JSON-RPC messages to /mcp", close,
                      "Allow: POST\r\n");
                return;
            }

            json msg = json::parse(r.body.toStdString(), nullptr, false);
            if (msg.is_discarded()) {
                reply(s, 400, "application/json",
                      json{{"jsonrpc", "2.0"},
                           {"id", nullptr},
                           {"error", {{"code", -32700}, {"message", "parse error"}}}}
                          .dump(),
                      close);
                return;
            }

            // A batch (older revisions) or a single message.
            json replies = json::array();
            for (const json &m : msg.is_array() ? msg : json::array({msg})) {
                if (std::optional<json> out = handler_(m)) {
                    replies.push_back(std::move(*out));
                }
            }
            if (replies.empty()) {
                reply(s, 202, "", "", close); // notifications / responses only
                return;
            }
            reply(s, 200, "application/json",
                  (msg.is_array() ? replies : replies[0]).dump(), close);
        }

        static void reply(QTcpSocket *s, int status, const QByteArray &type,
                          const std::string &body, bool close, const QByteArray &extra = {}) {
            const char *reason = status == 200   ? "OK"
                                 : status == 202 ? "Accepted"
                                 : status == 400 ? "Bad Request"
                                 : status == 403 ? "Forbidden"
                                 : status == 404 ? "Not Found"
                                 : status == 405 ? "Method Not Allowed"
                                                 : "Error";
            QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
            if (!type.isEmpty()) {
                head += "Content-Type: " + type + "\r\n";
            }
            head += "Content-Length: " + QByteArray::number(qsizetype(body.size())) + "\r\n";
            head += extra;
            head += close ? "Connection: close\r\n\r\n" : "Connection: keep-alive\r\n\r\n";
            s->write(head);
            s->write(body.data(), qint64(body.size()));
            if (close) {
                s->disconnectFromHost();
            }
        }

        QTcpServer                         tcp_;
        Handler                            handler_;
        std::map<QTcpSocket *, QByteArray> buffers_;
    };

} // namespace rosetta::mcp
