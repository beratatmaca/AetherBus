#include "gui/control/control_bridge.hpp"

#include "gui/mainwindow.hpp"
#include "gui/sessions/session_view.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace aether {

namespace {

QString sessionTypeName(SessionType type) {
    switch (type) {
        case SessionType::Serial:
            return QStringLiteral("serial");
        case SessionType::Can:
            return QStringLiteral("can");
        case SessionType::Ethernet:
            return QStringLiteral("ethernet");
        case SessionType::Usb:
            return QStringLiteral("usb");
    }
    return QStringLiteral("serial");
}

/// Serialize a reply object to the compact NDJSON the server forwards verbatim.
QByteArray packReply(const QJsonObject &obj) {
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

}  // namespace

ControlBridge::ControlBridge(QObject *parent) : QObject(parent) {
    // Needed for the cross-thread queued `traffic` signal delivery.
    qRegisterMetaType<QVector<aether::CapturedChunk>>("QVector<aether::CapturedChunk>");
}

void ControlBridge::registerSession(SessionView *session) {
    if (session == nullptr || session->controlId() == 0) {
        return;
    }
    const int id = session->controlId();
    m_sessions.insert(id, session);
    connect(session, &SessionView::controlTraffic, this, [this, id](const QVector<CapturedChunk> &chunks) { emit traffic(id, chunks); });
}

void ControlBridge::unregisterSession(int sessionId) {
    m_sessions.remove(sessionId);
    // Drop any repeating sends aimed at the now-gone session.
    const QList<int> scheduleIds = m_schedules.keys();
    for (int scheduleId : scheduleIds) {
        if (m_schedules.value(scheduleId).sessionId == sessionId) {
            cancelSchedule(scheduleId);
        }
    }
}

QByteArray ControlBridge::listSessionsJson() const {
    QJsonArray arr;
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        SessionView *s = it.value();
        if (s == nullptr) {
            continue;
        }
        arr.append(QJsonObject{{QStringLiteral("id"), it.key()},
                               {QStringLiteral("type"), sessionTypeName(s->sessionType())},
                               {QStringLiteral("name"), s->sessionName()},
                               {QStringLiteral("running"), s->isRunning()}});
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

QByteArray ControlBridge::sessionVerb(int sessionId, const QString &verb, const QJsonObject &args) {
    SessionView *session = m_sessions.value(sessionId).data();
    if (session == nullptr) {
        return packReply(
            {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("unknown session id %1").arg(sessionId)}});
    }
    QJsonObject reply;
    QString error;
    const bool ok = session->handleControl(verb, args, reply, &error);
    reply.insert(QStringLiteral("ok"), ok);
    if (!ok) {
        reply.insert(QStringLiteral("error"), error.isEmpty() ? QStringLiteral("%1 failed").arg(verb) : error);
    }
    return packReply(reply);
}

QByteArray ControlBridge::openSession(const QString &type, const QJsonObject &config, bool start) {
    const auto err = [](const QString &msg) { return packReply({{QStringLiteral("ok"), false}, {QStringLiteral("error"), msg}}); };

    if (m_window == nullptr) {
        return err(QStringLiteral("session creation is unavailable"));
    }
    SessionType st{};
    if (type == QLatin1String("serial")) {
        st = SessionType::Serial;
    } else if (type == QLatin1String("can")) {
        st = SessionType::Can;
    } else if (type == QLatin1String("ethernet")) {
        st = SessionType::Ethernet;
    } else {
        return err(QStringLiteral("open: 'type' must be serial, can or ethernet"));
    }

    QString error;
    const int id = m_window->createControlSession(st, &error);
    if (id == 0) {
        return err(error.isEmpty() ? QStringLiteral("could not create session") : error);
    }
    SessionView *session = m_sessions.value(id).data();
    if (session == nullptr) {
        return err(QStringLiteral("session created but not registered"));
    }
    if (!config.isEmpty() && !session->applyControlConfig(config, &error)) {
        return err(error);
    }
    if (start && !session->startSession(&error)) {
        return err(error);
    }
    // Return the new id under "session" (not "id") — the server reserves the
    // top-level "id" field for request-correlation and would overwrite it.
    return packReply({{QStringLiteral("ok"), true}, {QStringLiteral("session"), id}});
}

QString ControlBridge::closeSession(int sessionId) {
    SessionView *session = m_sessions.value(sessionId).data();
    if (session == nullptr) {
        return QStringLiteral("unknown session id %1").arg(sessionId);
    }
    session->close();  // WA_DeleteOnClose → destroyed handler unregisters it
    return {};
}

QByteArray ControlBridge::scheduleSend(int sessionId, const QJsonObject &args) {
    const auto err = [](const QString &msg) { return packReply({{QStringLiteral("ok"), false}, {QStringLiteral("error"), msg}}); };

    if (m_sessions.value(sessionId).data() == nullptr) {
        return err(QStringLiteral("unknown session id %1").arg(sessionId));
    }
    const int interval = args.value(QStringLiteral("interval_ms")).toInt(0);
    if (interval < 10) {
        return err(QStringLiteral("schedule_send: 'interval_ms' must be >= 10"));
    }
    int remaining = -1;  // no 'count' => repeat until cancelled
    if (args.contains(QStringLiteral("count"))) {
        remaining = args.value(QStringLiteral("count")).toInt(0);
        if (remaining <= 0) {
            return err(QStringLiteral("schedule_send: 'count' must be a positive integer"));
        }
    }

    // The send payload is the request minus the control-only keys.
    QJsonObject sendArgs = args;
    for (const char *key : {"cmd", "id", "session", "interval_ms", "count"}) {
        sendArgs.remove(QLatin1String(key));
    }

    const int scheduleId = ++m_nextScheduleId;
    auto *timer = new QTimer(this);
    timer->setInterval(interval);
    m_schedules.insert(scheduleId, ScheduleEntry{timer, sessionId, sendArgs, remaining});
    connect(timer, &QTimer::timeout, this, [this, scheduleId] { onScheduleTick(scheduleId); });
    timer->start();
    return packReply({{QStringLiteral("ok"), true}, {QStringLiteral("schedule"), scheduleId}});
}

QString ControlBridge::cancelSchedule(int scheduleId) {
    auto it = m_schedules.find(scheduleId);
    if (it == m_schedules.end()) {
        return QStringLiteral("unknown schedule id %1").arg(scheduleId);
    }
    it->timer->stop();
    it->timer->deleteLater();
    m_schedules.erase(it);
    return {};
}

void ControlBridge::onScheduleTick(int scheduleId) {
    auto it = m_schedules.find(scheduleId);
    if (it == m_schedules.end()) {
        return;
    }
    SessionView *session = m_sessions.value(it->sessionId).data();
    if (session == nullptr) {
        cancelSchedule(scheduleId);  // target session gone
        return;
    }
    QJsonObject reply;
    QString error;
    session->handleControl(QStringLiteral("send"), it->sendArgs, reply, &error);  // best-effort; per-tick errors ignored
    if (it->remaining > 0) {
        it->remaining -= 1;
        if (it->remaining == 0) {
            cancelSchedule(scheduleId);
        }
    }
}

bool ControlBridge::hasSession(int sessionId) const {
    return m_sessions.value(sessionId).data() != nullptr;
}

}  // namespace aether
