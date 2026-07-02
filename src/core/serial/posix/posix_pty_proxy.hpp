#pragma once
#include "core/common/pcap_writer.hpp"
#include "core/serial/pty_proxy_impl.hpp"
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

namespace aether {

/**
 * @brief Shared POSIX pseudo-terminal man-in-the-middle proxy.
 *
 * Implements the full interception engine on any Unix that provides
 * @c posix_openpt / @c grantpt / @c ptsname, @c poll(), termios packet mode
 * (@c TIOCPKT) and the @c TIOCM* modem-line ioctls — i.e. both Linux and macOS.
 * The physical UART and the PTY master are multiplexed on a single @c poll()
 * worker thread; data read from the UART is tagged @ref Direction::Rx and
 * forwarded to the master, data read from the master is tagged
 * @ref Direction::Tx and forwarded to the UART.
 *
 * The only platform-specific behaviour is applying a non-standard baud rate,
 * which each concrete subclass supplies via @ref applyCustomBaud (Linux uses
 * @c termios2 / @c BOTHER; macOS uses @c IOSSIOSPEED).
 */
class PosixPtyProxy : public PtyProxyImpl {
public:
    explicit PosixPtyProxy(PtyProxy *q);
    ~PosixPtyProxy() override;

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

protected:
    /**
     * @brief Apply an exact, non-standard baud rate to the open UART fd.
     *
     * Called only when the requested rate has no @c Bxxxx termios constant on
     * this platform. Subclasses supply the platform ioctl path.
     * @return @c true on success.
     */
    virtual bool applyCustomBaud(int fd, int baud) = 0;

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

    PcapWriter m_pcap;
};

}  // namespace aether
