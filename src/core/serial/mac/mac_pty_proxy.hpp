#pragma once
#include "core/serial/pty_proxy_impl.hpp"

namespace aether {

class MacPtyProxy : public PtyProxyImpl {
public:
    explicit MacPtyProxy(PtyProxy *q);
    ~MacPtyProxy() override;

    bool open(const SerialConfig &config) override;
    void close() override;
    bool isRunning() const override;
    QString slavePath() const override;
    bool injectToDevice(const QByteArray &bytes) override;
    bool injectToApp(const QByteArray &bytes) override;

    PtyProxy::ModemLines modemLines() const override;
    bool setRts(bool on) override;
    bool setDtr(bool on) override;
    bool sendBreak() override;

    bool startCapture(const QString &path) override;
    void stopCapture() override;
    bool isCapturing() const override;

    PtyProxy::Stats stats() const override;
};

}  // namespace aether
