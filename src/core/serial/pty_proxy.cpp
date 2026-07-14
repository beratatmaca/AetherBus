#include "core/serial/pty_proxy.hpp"
#include "core/serial/pty_proxy_impl.hpp"
#include <QtGlobal>

#if defined(Q_OS_WIN)
#include "core/serial/win/win_pty_proxy.hpp"
#elif defined(Q_OS_MAC)
#include "core/serial/mac/mac_pty_proxy.hpp"
#else
#include "core/serial/linux/linux_pty_proxy.hpp"
#endif

namespace aether {

PtyProxy::PtyProxy(QObject *parent) : IBusBackend(parent) {
    qRegisterMetaType<aether::CapturedChunk>("aether::CapturedChunk");
    qRegisterMetaType<QVector<aether::CapturedChunk>>("QVector<aether::CapturedChunk>");
    qRegisterMetaType<aether::Direction>("aether::Direction");
    qRegisterMetaType<aether::Parity>("aether::Parity");

#if defined(Q_OS_WIN)
    d = std::make_unique<WindowsPtyProxy>(this);
#elif defined(Q_OS_MAC)
    d = std::make_unique<MacPtyProxy>(this);
#else
    d = std::make_unique<LinuxPtyProxy>(this);
#endif
}

PtyProxy::~PtyProxy() {
    PtyProxy::close();  // qualified: non-virtual dispatch is intended in a dtor
}

bool PtyProxy::open(const SerialConfig &config) {
    return d->open(config);
}

void PtyProxy::close() {
    d->close();
}

bool PtyProxy::isRunning() const {
    return d->isRunning();
}

QString PtyProxy::slavePath() const {
    return d->slavePath();
}

bool PtyProxy::injectToDevice(const QByteArray &bytes) {
    return d->injectToDevice(bytes);
}

bool PtyProxy::injectToApp(const QByteArray &bytes) {
    return d->injectToApp(bytes);
}

PtyProxy::ModemLines PtyProxy::modemLines() const {
    return d->modemLines();
}

bool PtyProxy::setRts(bool on) const {
    return d->setRts(on);
}

bool PtyProxy::setDtr(bool on) const {
    return d->setDtr(on);
}

bool PtyProxy::sendBreak() const {
    return d->sendBreak();
}

PtyProxy::Stats PtyProxy::stats() const {
    return d->stats();
}

bool PtyProxy::startCapture(const QString &path) {
    return d->startCapture(path);
}

void PtyProxy::stopCapture() {
    d->stopCapture();
}

bool PtyProxy::isCapturing() const {
    return d->isCapturing();
}

}  // namespace aether
