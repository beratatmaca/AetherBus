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
#include "gui/sessions/session_view.hpp"  // SessionType

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QVector>

class QTimer;

namespace aether {

class SessionView;
class MainWindow;

class ControlBridge : public QObject {
    Q_OBJECT

public:
    explicit ControlBridge(QObject *parent = nullptr);

    // Called on the GUI thread by MainWindow.
    void registerSession(SessionView *session);
    void unregisterSession(int sessionId);

    /** @brief Host window used by @ref openSession / @ref closeSession to create/destroy tabs. */
    void setWindow(MainWindow *window) { m_window = window; }

    // --- Invoked from the ControlServer's worker thread (BlockingQueued) ---
    // All run on the GUI thread, so they may safely touch session widgets.

    /** @brief Compact-JSON array of the open sessions (the server wraps it into the reply). */
    [[nodiscard]] Q_INVOKABLE QByteArray listSessionsJson() const;

    /**
     * @brief Execute a session-scoped @p verb (send/start/stop/stats/capture/replay/run_macro)
     * against session @p id, delegating to @c SessionView::handleControl.
     * @return a compact-JSON reply object (`{ok, error?, ...verb data}`) the server forwards.
     */
    Q_INVOKABLE QByteArray sessionVerb(int sessionId, const QString &verb, const QJsonObject &args);

    /**
     * @brief Create a new session of @p type, apply @p config, and optionally @p start it.
     * @return a compact-JSON reply object (`{ok, id}` or `{ok:false, error}`).
     */
    Q_INVOKABLE QByteArray openSession(const QString &type, const QJsonObject &config, bool start);

    /** @brief Close (and destroy) session @p id; empty string on success, else an error. */
    Q_INVOKABLE QString closeSession(int sessionId);

    /**
     * @brief Register a repeating send against session @p id (`send` fields + `interval_ms`,
     * optional `count`); the timer lives on the GUI thread and re-runs the send each tick.
     * @return a compact-JSON reply object (`{ok, schedule}` or `{ok:false, error}`).
     */
    Q_INVOKABLE QByteArray scheduleSend(int sessionId, const QJsonObject &args);

    /** @brief Cancel a schedule created by @ref scheduleSend; empty string on success, else an error. */
    Q_INVOKABLE QString cancelSchedule(int scheduleId);

    /** @brief Whether a session with @p sessionId is currently registered. */
    [[nodiscard]] Q_INVOKABLE bool hasSession(int sessionId) const;

signals:
    /** @brief Emitted on the GUI thread when a registered session captures traffic. */
    void traffic(int sessionId, const QVector<aether::CapturedChunk> &chunks);

private:
    /** @brief Fire one tick of schedule @p scheduleId, decrementing its remaining count. */
    void onScheduleTick(int scheduleId);

    /** @brief A repeating send: its timer, target session, the send args, and remaining shots (-1 = forever). */
    struct ScheduleEntry {
        QTimer *timer = nullptr;
        int sessionId = 0;
        QJsonObject sendArgs;
        int remaining = -1;
    };

    QHash<int, QPointer<SessionView>> m_sessions;  ///< controlId -> session (GUI-thread only)
    QHash<int, ScheduleEntry> m_schedules;         ///< scheduleId -> repeating send (GUI-thread only)
    int m_nextScheduleId = 0;
    MainWindow *m_window = nullptr;
};

}  // namespace aether
