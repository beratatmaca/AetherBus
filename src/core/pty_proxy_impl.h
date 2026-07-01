#pragma once
#include "core/pty_proxy.h"

namespace aether {

class PtyProxyImpl {
public:
    explicit PtyProxyImpl(PtyProxy *q) : q_ptr(q) {}
    virtual ~PtyProxyImpl() = default;

    virtual bool open(const SerialConfig &config) = 0;
    virtual void close() = 0;
    virtual bool isRunning() const = 0;
    virtual QString slavePath() const = 0;
    virtual bool injectToDevice(const QByteArray &bytes) = 0;
    virtual bool injectToApp(const QByteArray &bytes) = 0;

    virtual PtyProxy::ModemLines modemLines() const = 0;
    virtual bool setRts(bool on) = 0;
    virtual bool setDtr(bool on) = 0;
    virtual bool sendBreak() = 0;

    virtual bool startCapture(const QString &path) = 0;
    virtual void stopCapture() = 0;
    virtual bool isCapturing() const = 0;

    virtual PtyProxy::Stats stats() const = 0;

protected:
    PtyProxy *q_ptr; // pointer to public QObject wrapper to emit signals
};

} // namespace aether
