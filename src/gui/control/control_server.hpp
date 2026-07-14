/**
 * @file control_server.hpp
 * @brief Worker-thread transport for the localhost control channel.
 *
 * Runs entirely on its own thread (moved there by MainWindow): it owns the
 * @c QLocalServer and every client socket, does all JSON framing/encoding, and
 * enforces per-client backpressure — so high-rate traffic streaming never
 * competes with the GUI thread. It never touches session objects directly;
 * all session state goes through @c ControlBridge (GUI thread) via
 * blocking-queued invocations, and traffic arrives via a queued signal.
 *
 * Transport: a Unix domain socket (owner-only, 0600) on Linux/macOS, a per-user
 * named pipe on Windows. Wire protocol: newline-delimited JSON (see the Python
 * client in the pip package).
 */
#pragma once

#include "core/serial/serial_types.hpp"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QVector>

class QLocalServer;
class QLocalSocket;
class QJsonObject;

namespace aether {

class ControlBridge;

class ControlServer : public QObject {
    Q_OBJECT

public:
    explicit ControlServer(QObject *parent = nullptr);
    ~ControlServer() override;

    /** @brief Set the GUI-thread bridge; must be called before @ref start. */
    void setBridge(ControlBridge *bridge) { m_bridge = bridge; }

    /** @brief Platform socket name/path; the Python client mirrors this. */
    [[nodiscard]] static QString socketName();

public slots:
    /** @brief Begin listening (run on the worker thread, e.g. from QThread::started). */
    void start();
    /** @brief Close the server and drop all clients (run on the worker thread). */
    void stop();

    /** @brief Queued from ControlBridge::traffic; encode + write to subscribed clients. */
    void onTraffic(int sessionId, const QVector<aether::CapturedChunk> &chunks);

private:
    /** @brief Per-connection state: partial-line buffer + subscribed session ids. */
    struct Client {
        QByteArray buffer;
        QSet<int> subscriptions;
        qint64 dropped = 0;  ///< chunks shed under backpressure since the last notice
    };

    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);
    void handleLine(QLocalSocket *socket, const QByteArray &line);
    void writeMessage(QLocalSocket *socket, const QJsonObject &obj);

    QLocalServer *m_server = nullptr;
    ControlBridge *m_bridge = nullptr;
    QHash<QLocalSocket *, Client> m_clients;

    /// Per-client write-buffer ceiling; a slow consumer gets chunks dropped
    /// rather than back-pressuring the GUI thread's traffic signal.
    static constexpr qint64 kMaxQueuedBytes = qint64{4} * 1024 * 1024;
};

}  // namespace aether
