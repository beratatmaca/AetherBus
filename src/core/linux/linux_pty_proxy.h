#pragma once
#include "core/pty_proxy_impl.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <QFile>

namespace aether {

class LinuxPtyProxy : public PtyProxyImpl {
public:
    explicit LinuxPtyProxy(PtyProxy *q);
    ~LinuxPtyProxy() override;

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

private:
    struct OutQueue {
        std::deque<QByteArray> chunks;
        std::size_t frontOffset = 0;
        std::size_t queued = 0;
    };

    void runLoop();
    bool configureTermios(const SerialConfig &config);
    void mirrorSlaveTermios();
    void wakeLoop() const;
    void teardownDescriptors();
    bool forward(int fd, OutQueue &queue, Direction dir, const QByteArray &data);
    static bool drainLocked(int fd, OutQueue &queue);
    void writePcapPacket(qint64 timestampMs, Direction dir, const QByteArray &data);

    static constexpr std::size_t kMaxQueueBytes = std::size_t{8} * 1024 * 1024;

    int m_uartFd = -1;
    int m_masterFd = -1;
    int m_wakeReadFd = -1;
    int m_wakeWriteFd = -1;

    QString m_slavePath;
    QString m_symlinkPath;
    int m_cleanupSlot = -1;

    std::thread m_worker;
    std::mutex m_writeMutex;
    OutQueue m_uartOut;
    OutQueue m_masterOut;
    qint64 m_lastStallMs = 0;
    std::atomic<std::uint64_t> m_rxBytes{0};
    std::atomic<std::uint64_t> m_txBytes{0};
    std::atomic<std::uint64_t> m_droppedBytes{0};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_directMode{false};
    std::atomic<bool> m_ioFailed{false};

    int m_lastBaud = 0;
    int m_lastDataBits = 0;
    Parity m_lastParity = Parity::None;
    int m_lastStopBits = 0;

    mutable std::mutex m_captureMutex;
    std::unique_ptr<QFile> m_captureFile;
};

}  // namespace aether
