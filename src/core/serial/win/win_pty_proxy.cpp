#include "core/serial/win/win_pty_proxy.hpp"

namespace aether {

WindowsPtyProxy::WindowsPtyProxy(PtyProxy *q) : PtyProxyImpl(q) {}

WindowsPtyProxy::~WindowsPtyProxy() {
    close();
}

bool WindowsPtyProxy::open(const SerialConfig &) {
    emit q_ptr->errorOccurred(q_ptr->tr("Windows PtyProxy implementation is not yet complete."));
    return false;
}

void WindowsPtyProxy::close() {}

bool WindowsPtyProxy::isRunning() const {
    return false;
}

QString WindowsPtyProxy::slavePath() const {
    return {};
}

bool WindowsPtyProxy::injectToDevice(const QByteArray &) {
    return false;
}

bool WindowsPtyProxy::injectToApp(const QByteArray &) {
    return false;
}

PtyProxy::ModemLines WindowsPtyProxy::modemLines() const {
    return {};
}

bool WindowsPtyProxy::setRts(bool) {
    return false;
}

bool WindowsPtyProxy::setDtr(bool) {
    return false;
}

bool WindowsPtyProxy::sendBreak() {
    return false;
}

bool WindowsPtyProxy::startCapture(const QString &) {
    return false;
}

void WindowsPtyProxy::stopCapture() {}

bool WindowsPtyProxy::isCapturing() const {
    return false;
}

PtyProxy::Stats WindowsPtyProxy::stats() const {
    return {};
}

}  // namespace aether
