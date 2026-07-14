#pragma once

#include "core/common/i_bus_backend.hpp"
#include "core/serial/serial_types.hpp"

#include <QString>
#include <memory>

namespace aether {

class PtyProxyImpl;

/**
 * @brief Serial man-in-the-middle proxy backend.
 *
 * Opens the physical serial device and exposes a virtual side to the target
 * application (a slave PTY on POSIX, a named pipe on Windows), capturing and
 * forwarding traffic in both directions. A thin facade: every call is
 * delegated to the platform-specific @ref PtyProxyImpl chosen at construction.
 */
class PtyProxy : public IBusBackend {
    Q_OBJECT

public:
    explicit PtyProxy(QObject *parent = nullptr);
    ~PtyProxy() override;

    PtyProxy(const PtyProxy &) = delete;
    PtyProxy &operator=(const PtyProxy &) = delete;

    /** @brief Open the physical device described by @p config and start the proxy worker. */
    bool open(const SerialConfig &config);
    void close() override;

    [[nodiscard]] bool isRunning() const override;
    /** @brief Path of the virtual side handed to the target application (slave PTY node or pipe name). */
    [[nodiscard]] QString slavePath() const;

    /** @brief Write @p bytes directly to the physical device, captured as Tx traffic. */
    bool injectToDevice(const QByteArray &bytes);
    /** @brief Write @p bytes to the connected application, captured as Rx traffic. */
    bool injectToApp(const QByteArray &bytes);

    /** @brief Snapshot of the physical port's modem/handshake line states. */
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

    /** @brief Cumulative traffic counters since @ref open (bytes forwarded and dropped). */
    struct Stats {
        quint64 rx = 0;
        quint64 tx = 0;
        quint64 dropped = 0;
    };

    [[nodiscard]] Stats stats() const;

    /** @brief Start recording all captured traffic to a pcap file at @p path. */
    bool startCapture(const QString &path);
    void stopCapture();
    [[nodiscard]] bool isCapturing() const;

signals:
    // Common capture/lifecycle signals (chunkCaptured, started, stopped,
    // errorOccurred, disconnected) are inherited from IBusBackend. Only the
    // serial-specific extras are declared here.
    /** @brief The application changed the line settings on the virtual port; they were mirrored to the device. */
    void lineReconfigured(int baud, int dataBits, aether::Parity parity, int stopBits);
    /** @brief A destination stopped accepting data and bytes were dropped; @p droppedTotal is the running total. */
    void writeStalled(aether::Direction dir, quint64 droppedTotal);

private:
    std::unique_ptr<PtyProxyImpl> d;
};

}  // namespace aether
