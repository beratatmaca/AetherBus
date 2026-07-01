/**
 * @file pty_proxy.h
 * @brief Interceptty-style man-in-the-middle serial proxy.
 */
#pragma once

#include "core/serial_types.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

class QFile;

namespace aether {

/**
 * @brief Transparently proxies a physical UART through a kernel pseudo-terminal.
 *
 * Opens the real device and a master/slave PTY pair, then runs a background
 * @c poll() loop that shuttles bytes between them while emitting a
 * direction-tagged @ref CapturedChunk copy of every transfer for the GUI.
 *
 * @par Threading contract
 * @ref open(), @ref close() and the @c inject*() / line-control methods are
 * called from the GUI thread. @ref chunkCaptured() and @ref disconnected() are
 * emitted from the worker thread and delivered to GUI slots via queued
 * connections (@ref CapturedChunk is a registered metatype).
 *
 * @note POSIX-only; compiled exclusively on UNIX platforms.
 */
class PtyProxy : public QObject {
    Q_OBJECT

public:
    /** @brief Construct an idle proxy. @param parent Optional QObject parent. */
    explicit PtyProxy(QObject *parent = nullptr);
    ~PtyProxy() override;

    PtyProxy(const PtyProxy &) = delete;
    PtyProxy &operator=(const PtyProxy &) = delete;

    /**
     * @brief Open the physical device (and a fresh PTY pair) and start the loop.
     * @param config Device path and line settings to apply.
     * @return @c true on success; on failure emits @ref errorOccurred() and
     *         returns @c false. On success the virtual port is @ref slavePath().
     */
    bool open(const SerialConfig &config);

    /**
     * @brief Stop the worker loop and release every descriptor and symlink.
     * @note Safe to call repeatedly and from the destructor.
     */
    void close();

    /** @return @c true while the interception loop is running. */
    [[nodiscard]] bool isRunning() const { return m_running.load(); }

    /** @return Kernel-assigned slave PTY path (e.g. @c /dev/pts/5) after @ref open(). */
    [[nodiscard]] QString slavePath() const { return m_slavePath; }

    /**
     * @brief Write raw bytes straight to the physical UART (acts as the host).
     * @param bytes Payload to transmit.
     * @return @c true if the bytes were written.
     */
    bool injectToDevice(const QByteArray &bytes);

    /**
     * @brief Write raw bytes to the PTY master, so the target app sees them as Rx.
     * @param bytes Payload to deliver to the target application.
     * @return @c true if the bytes were written.
     */
    bool injectToApp(const QByteArray &bytes);

    /** @brief Snapshot of the modem control lines on the physical UART. */
    struct ModemLines {
        bool cts = false;  ///< Clear To Send (input).
        bool dsr = false;  ///< Data Set Ready (input).
        bool dcd = false;  ///< Data Carrier Detect (input).
        bool ri = false;   ///< Ring Indicator (input).
        bool rts = false;  ///< Request To Send (output).
        bool dtr = false;  ///< Data Terminal Ready (output).
    };

    /** @return The current modem control lines read from the physical UART. */
    [[nodiscard]] ModemLines modemLines() const;

    /**
     * @brief Drive the RTS output line on the physical UART.
     * @param on Desired line state.
     * @return @c true on success.
     */
    [[nodiscard]] bool setRts(bool on) const;

    /**
     * @brief Drive the DTR output line on the physical UART.
     * @param on Desired line state.
     * @return @c true on success.
     */
    [[nodiscard]] bool setDtr(bool on) const;

    /**
     * @brief Send a serial break condition on the physical UART.
     * @return @c true on success.
     */
    [[nodiscard]] bool sendBreak() const;

    /**
     * @brief Wire-traffic counters maintained by the backend independently of
     *        the GUI render path, so they stay accurate even if the UI stalls.
     */
    struct Stats {
        quint64 rx = 0;       ///< Bytes received from the physical UART.
        quint64 tx = 0;       ///< Bytes received from the target app (PTY master).
        quint64 dropped = 0;  ///< Bytes dropped after a write queue hit its ceiling.
    };

    /** @return A lock-free snapshot of the backend byte counters. */
    [[nodiscard]] Stats stats() const;

    /**
     * @brief Begin recording all captured traffic to a pcap file
     *        (@c LINKTYPE_RTAC_SERIAL), written straight from the worker thread.
     * @param path Destination file (truncated if it exists).
     * @return @c true if the file was opened; otherwise emits @ref errorOccurred().
     */
    bool startCapture(const QString &path);

    /** @brief Stop and finalize the active pcap capture, if any. */
    void stopCapture();

    /** @return @c true while a pcap capture is being written. */
    [[nodiscard]] bool isCapturing() const;

signals:
    /** @brief Emitted for every captured transfer. @param chunk Timestamped, tagged bytes. */
    void chunkCaptured(const aether::CapturedChunk &chunk);
    /** @brief Emitted on any open/runtime failure. @param message Human-readable reason. */
    void errorOccurred(const QString &message);
    /** @brief Emitted once interception starts. @param slavePath The virtual port path. */
    void started(const QString &slavePath);
    /** @brief Emitted once the loop has fully stopped and descriptors are released. */
    void stopped();

    /**
     * @brief Emitted (queued) when the multiplexing loop aborts unexpectedly,
     *        e.g. the device was unplugged. Drives optional GUI auto-reconnect.
     */
    void disconnected();

    /**
     * @brief Emitted when the target application reconfigures the slave PTY line
     *        settings and the backend has mirrored them onto the physical UART.
     * @param baud     New baud rate (0 if it is a non-standard rate).
     * @param dataBits New data bits (5..8).
     * @param parity   New parity mode.
     * @param stopBits New stop bits (1 or 2).
     */
    void lineReconfigured(int baud, int dataBits, aether::Parity parity, int stopBits);

    /**
     * @brief Emitted (throttled to ~1 Hz) when a write queue overflowed and bytes
     *        were dropped because the peer stopped draining.
     * @param dir          Stream whose destination is backlogged.
     * @param droppedTotal Cumulative dropped-byte count since @ref open().
     */
    void writeStalled(aether::Direction dir, quint64 droppedTotal);

private:
    /// A pending-write FIFO for one output descriptor, drained on @c POLLOUT.
    struct OutQueue {
        std::deque<QByteArray> chunks;  ///< Queued payloads awaiting write.
        std::size_t frontOffset = 0;    ///< Bytes of chunks.front() already written.
        std::size_t queued = 0;         ///< Total unwritten bytes across the queue.
    };

    void runLoop();                                     // worker-thread body
    bool configureTermios(const SerialConfig &config);  // apply baud/bits/parity
    void mirrorSlaveTermios();                          // copy slave-PTY line settings to the UART
    void wakeLoop() const;                              // poke the self-pipe to break poll()
    void teardownDescriptors();
    // Enqueue bytes for a destination fd and drain what it can accept now. Drops
    // (and accounts) when the queue is over its ceiling; returns false on drop or
    // a fatal write error. @p dir tags which stream stalled.
    bool forward(int fd, OutQueue &queue, Direction dir, const QByteArray &data);
    // Write as much of @p queue to @p fd as it accepts without blocking. Caller
    // must hold @ref m_writeMutex. Returns false on a fatal write error.
    static bool drainLocked(int fd, OutQueue &queue);
    // Append one pcap record for @p data; no-op when no capture is active.
    void writePcapPacket(qint64 timestampMs, Direction dir, const QByteArray &data);

    /// Upper bound on per-destination buffered bytes before new writes are dropped.
    static constexpr std::size_t kMaxQueueBytes = std::size_t{8} * 1024 * 1024;

    int m_uartFd = -1;       // physical device
    int m_masterFd = -1;     // PTY master
    int m_wakeReadFd = -1;   // self-pipe read end (loop watches this)
    int m_wakeWriteFd = -1;  // self-pipe write end (close() writes here)

    QString m_slavePath;
    QString m_symlinkPath;   // created symlink, if any (unlinked on close)
    int m_cleanupSlot = -1;  // signal-safe cleanup registry handle (see signal_cleanup.h)

    std::thread m_worker;
    std::mutex m_writeMutex;   // guards the out-queues and the stall timestamp
    OutQueue m_uartOut;        // bytes bound for the physical UART (Tx / inject-to-device)
    OutQueue m_masterOut;      // bytes bound for the PTY master (Rx / inject-to-app)
    qint64 m_lastStallMs = 0;  // last writeStalled() emission (guarded by m_writeMutex)
    std::atomic<std::uint64_t> m_rxBytes{0};
    std::atomic<std::uint64_t> m_txBytes{0};
    std::atomic<std::uint64_t> m_droppedBytes{0};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_directMode{false};
    std::atomic<bool> m_ioFailed{false};  // set by a fatal write; runLoop breaks on it

    // Last line settings mirrored from the slave (worker-thread only), used to
    // suppress duplicate lineReconfigured() emissions.
    int m_lastBaud = 0;
    int m_lastDataBits = 0;
    Parity m_lastParity = Parity::None;
    int m_lastStopBits = 0;

    mutable std::mutex m_captureMutex;     // guards m_captureFile
    std::unique_ptr<QFile> m_captureFile;  // active pcap capture, or null
};

}  // namespace aether
