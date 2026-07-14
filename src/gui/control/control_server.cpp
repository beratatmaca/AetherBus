#include "gui/control/control_server.hpp"

#include "gui/control/control_bridge.hpp"
#include "aether/version.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalServer>
#include <QLocalSocket>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace aether {

namespace {
/// Wire-protocol version, greeted in the hello line. Bump on incompatible changes.
constexpr int kProtocolVersion = 1;
}  // namespace

ControlServer::ControlServer(QObject *parent) : QObject(parent) {}

ControlServer::~ControlServer() = default;

QString ControlServer::socketName() {
#if defined(Q_OS_WIN)
    // Maps to \\.\pipe\aetherbus-control; the pipe's default ACL is per-user.
    return QStringLiteral("aetherbus-control");
#else
    QString base = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (base.isEmpty()) {
        base = QDir::tempPath();
    }
    return QStringLiteral("%1/aetherbus-%2.sock").arg(base).arg(static_cast<uint>(::getuid()));
#endif
}

void ControlServer::start() {
    if (m_server != nullptr) {
        return;  // already listening
    }
    m_server = new QLocalServer(this);
    // Owner-only access: only processes running as this user can connect.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &ControlServer::onNewConnection);

    const QString name = socketName();
    QLocalServer::removeServer(name);  // clear a stale socket left by a crashed run
    if (!m_server->listen(name)) {
        qWarning("AetherBus control channel unavailable (%s): %s", qPrintable(name), qPrintable(m_server->errorString()));
    }
}

void ControlServer::stop() {
    if (m_server == nullptr) {
        return;
    }
    const auto sockets = m_clients.keys();
    for (QLocalSocket *socket : sockets) {
        socket->disconnectFromServer();
        socket->deleteLater();
    }
    m_clients.clear();
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
}

void ControlServer::onNewConnection() {
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        m_clients.insert(socket, Client{});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_clients.remove(socket);
            socket->deleteLater();
        });
        // Greet with the protocol version so clients can detect mismatches; it
        // carries an `event` key so command-reply readers skip it like any
        // other async line.
        writeMessage(socket, {{QStringLiteral("event"), QStringLiteral("hello")},
                              {QStringLiteral("protocol"), kProtocolVersion},
                              {QStringLiteral("version"), QStringLiteral(AETHER_VERSION_STRING)}});
    }
}

void ControlServer::onReadyRead(QLocalSocket *socket) {
    auto it = m_clients.find(socket);
    if (it == m_clients.end()) {
        return;
    }
    it->buffer.append(socket->readAll());
    int nl = -1;
    while ((nl = it->buffer.indexOf('\n')) >= 0) {
        const QByteArray line = it->buffer.left(nl);
        it->buffer.remove(0, nl + 1);
        if (!line.trimmed().isEmpty()) {
            handleLine(socket, line);
        }
    }
}

void ControlServer::handleLine(QLocalSocket *socket, const QByteArray &line) {
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        writeMessage(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("malformed JSON")}});
        return;
    }
    const QJsonObject cmd = doc.object();
    const QString verb = cmd.value(QStringLiteral("cmd")).toString();
    // Echo the client's request id so replies correlate even when async traffic
    // events interleave on the same socket.
    const QJsonValue reqId = cmd.value(QStringLiteral("id"));

    const auto reply = [&](bool ok, const QString &error = QString()) {
        QJsonObject obj{{QStringLiteral("ok"), ok}};
        if (!reqId.isUndefined()) {
            obj.insert(QStringLiteral("id"), reqId);
        }
        if (!ok) {
            obj.insert(QStringLiteral("error"), error);
        }
        writeMessage(socket, obj);
    };

    if (m_bridge == nullptr) {
        reply(false, QStringLiteral("control channel not ready"));
        return;
    }

    if (verb == QLatin1String("list")) {
        // Session state lives on the GUI thread — build the array there.
        QByteArray arr;
        QMetaObject::invokeMethod(m_bridge, "listSessionsJson", Qt::BlockingQueuedConnection, Q_RETURN_ARG(QByteArray, arr));
        QJsonObject obj{{QStringLiteral("ok"), true}, {QStringLiteral("sessions"), QJsonDocument::fromJson(arr).array()}};
        if (!reqId.isUndefined()) {
            obj.insert(QStringLiteral("id"), reqId);
        }
        writeMessage(socket, obj);
        return;
    }

    const int id = cmd.value(QStringLiteral("session")).toInt(-1);

    if (verb == QLatin1String("send")) {
        QString err;
        QMetaObject::invokeMethod(m_bridge, "doSend", Qt::BlockingQueuedConnection, Q_RETURN_ARG(QString, err), Q_ARG(int, id),
                                  Q_ARG(QJsonObject, cmd));
        reply(err.isEmpty(), err);
    } else if (verb == QLatin1String("subscribe")) {
        bool exists = false;
        QMetaObject::invokeMethod(m_bridge, "hasSession", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, exists), Q_ARG(int, id));
        if (!exists) {
            reply(false, QStringLiteral("unknown session id %1").arg(id));
            return;
        }
        m_clients[socket].subscriptions.insert(id);
        reply(true);
    } else if (verb == QLatin1String("unsubscribe")) {
        m_clients[socket].subscriptions.remove(id);
        reply(true);
    } else {
        reply(false, QStringLiteral("unknown cmd '%1'").arg(verb));
    }
}

void ControlServer::writeMessage(QLocalSocket *socket, const QJsonObject &obj) {
    socket->write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    socket->write("\n");
}

void ControlServer::onTraffic(int sessionId, const QVector<CapturedChunk> &chunks) {
    if (chunks.isEmpty() || m_clients.isEmpty()) {
        return;
    }

    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QLocalSocket *socket = it.key();
        Client &client = it.value();
        if (!client.subscriptions.contains(sessionId)) {
            continue;
        }

        // Backpressure: a slow consumer gets chunks dropped, never blocks us.
        if (socket->bytesToWrite() > kMaxQueuedBytes) {
            client.dropped += chunks.size();
            continue;
        }
        if (client.dropped > 0) {
            writeMessage(socket, {{QStringLiteral("event"), QStringLiteral("dropped")},
                                  {QStringLiteral("session"), sessionId},
                                  {QStringLiteral("count"), client.dropped}});
            client.dropped = 0;
        }

        // One batched `chunks` event per drain (not per chunk): fewer JSON docs
        // and a distinct `event` key so command-reply readers can skip it.
        QJsonArray arr;
        for (const CapturedChunk &chunk : chunks) {
            QJsonObject c{{QStringLiteral("dir"), chunk.dir == Direction::Tx ? QStringLiteral("tx") : QStringLiteral("rx")},
                          {QStringLiteral("ts"), static_cast<double>(chunk.timestampMs)},
                          {QStringLiteral("data"), QString::fromLatin1(chunk.data.toHex())}};
            if (chunk.isFrame) {
                c.insert(QStringLiteral("isFrame"), true);
                c.insert(QStringLiteral("frameId"), static_cast<double>(chunk.frameId));
                c.insert(QStringLiteral("flags"), static_cast<double>(chunk.frameFlags));
            }
            arr.append(c);
        }
        writeMessage(
            socket,
            {{QStringLiteral("event"), QStringLiteral("chunks")}, {QStringLiteral("session"), sessionId}, {QStringLiteral("chunks"), arr}});
    }
}

}  // namespace aether
