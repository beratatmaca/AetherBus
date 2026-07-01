#include "core/serial/mac/mac_pty_proxy.hpp"

namespace aether {

MacPtyProxy::MacPtyProxy(PtyProxy *q) : PtyProxyImpl(q) {}

MacPtyProxy::~MacPtyProxy() {
    close();
}

bool MacPtyProxy::open(const SerialConfig &) {
    emit q_ptr->errorOccurred(q_ptr->tr("macOS PtyProxy implementation is not yet complete."));
    return false;
}

void MacPtyProxy::close() {}

bool MacPtyProxy::isRunning() const {
    return false;
}

QString MacPtyProxy::slavePath() const {
    return {};
}

bool MacPtyProxy::injectToDevice(const QByteArray &) {
    return false;
}

bool MacPtyProxy::injectToApp(const QByteArray &) {
    return false;
}

PtyProxy::ModemLines MacPtyProxy::modemLines() const {
    return {};
}

bool MacPtyProxy::setRts(bool) {
    return false;
}

bool MacPtyProxy::setDtr(bool) {
    return false;
}

bool MacPtyProxy::sendBreak() {
    return false;
}

bool MacPtyProxy::startCapture(const QString &) {
    return false;
}

void MacPtyProxy::stopCapture() {}

bool MacPtyProxy::isCapturing() const {
    return false;
}

PtyProxy::Stats MacPtyProxy::stats() const {
    return {};
}

}  // namespace aether
