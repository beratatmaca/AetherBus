#pragma once

#include "core/serial_types.h"

#include <QObject>
#include <QString>
#include <memory>

namespace aether {

class PtyProxyImpl;

class PtyProxy : public QObject {
    Q_OBJECT

public:
    explicit PtyProxy(QObject *parent = nullptr);
    ~PtyProxy() override;

    PtyProxy(const PtyProxy &) = delete;
    PtyProxy &operator=(const PtyProxy &) = delete;

    bool open(const SerialConfig &config);
    void close();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString slavePath() const;

    bool injectToDevice(const QByteArray &bytes);
    bool injectToApp(const QByteArray &bytes);

    struct ModemLines {
        bool cts = false;
        bool dsr = false;
        bool dcd = false;
        bool ri = false;
        bool rts = false;
        bool dtr = false;
    };

    [[nodiscard]] ModemLines modemLines() const;
    [[nodiscard]] bool setRts(bool on) const;
    [[nodiscard]] bool setDtr(bool on) const;
    [[nodiscard]] bool sendBreak() const;

    struct Stats {
        quint64 rx = 0;
        quint64 tx = 0;
        quint64 dropped = 0;
    };

    [[nodiscard]] Stats stats() const;

    bool startCapture(const QString &path);
    void stopCapture();
    [[nodiscard]] bool isCapturing() const;

signals:
    void chunkCaptured(const aether::CapturedChunk &chunk);
    void errorOccurred(const QString &message);
    void started(const QString &slavePath);
    void stopped();
    void disconnected();
    void lineReconfigured(int baud, int dataBits, aether::Parity parity, int stopBits);
    void writeStalled(aether::Direction dir, quint64 droppedTotal);

private:
    std::unique_ptr<PtyProxyImpl> d;
};

}  // namespace aether
