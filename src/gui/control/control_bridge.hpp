/**
 * @file control_bridge.hpp
 * @brief GUI-thread half of the control channel.
 *
 * The @c ControlServer runs on its own worker thread and must never touch
 * QWidget-affine session objects directly. This bridge is the one object it is
 * allowed to reach session state through: the server invokes the @c Q_INVOKABLE
 * methods below with @c Qt::BlockingQueuedConnection (they run on the GUI
 * thread), and receives captured traffic via the queued @c traffic signal.
 */
#pragma once

#include "core/serial/serial_types.hpp"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>

class QJsonObject;

namespace aether {

class SessionView;

class ControlBridge : public QObject {
    Q_OBJECT

public:
    explicit ControlBridge(QObject *parent = nullptr);

    // Called on the GUI thread by MainWindow.
    void registerSession(SessionView *session);
    void unregisterSession(int sessionId);

    // --- Invoked from the ControlServer's worker thread (BlockingQueued) ---
    // All run on the GUI thread, so they may safely touch session widgets.

    /** @brief Compact-JSON array of the open sessions (the server wraps it into the reply). */
    [[nodiscard]] Q_INVOKABLE QByteArray listSessionsJson() const;

    /** @brief Execute a `send` command; returns an empty string on success or a human-readable error. */
    Q_INVOKABLE QString doSend(int sessionId, const QJsonObject &cmd);

    /** @brief Whether a session with @p sessionId is currently registered. */
    [[nodiscard]] Q_INVOKABLE bool hasSession(int sessionId) const;

signals:
    /** @brief Emitted on the GUI thread when a registered session captures traffic. */
    void traffic(int sessionId, const QVector<aether::CapturedChunk> &chunks);

private:
    QHash<int, QPointer<SessionView>> m_sessions;  ///< controlId -> session (GUI-thread only)
};

}  // namespace aether
