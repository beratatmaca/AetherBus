#include "gui/control/control_bridge.hpp"

#include "gui/sessions/session_view.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    }
    return QStringLiteral("serial");
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

QString ControlBridge::doSend(int sessionId, const QJsonObject &cmd) {
    SessionView *session = m_sessions.value(sessionId).data();
    if (session == nullptr) {
        return QStringLiteral("unknown session id %1").arg(sessionId);
    }
    QString error;
    if (!session->sendControl(cmd, &error)) {
        return error.isEmpty() ? QStringLiteral("send failed") : error;
    }
    return {};
}

bool ControlBridge::hasSession(int sessionId) const {
    return m_sessions.value(sessionId).data() != nullptr;
}

}  // namespace aether
