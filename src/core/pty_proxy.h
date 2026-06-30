/**
 * @file pty_proxy.h
 * @brief Interceptty-style man-in-the-middle serial proxy.
 */
#pragma once

#include "core/serial_types.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <mutex>
#include <thread>

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

private:
    void runLoop();                                     // worker-thread body
    bool configureTermios(const SerialConfig &config);  // apply baud/bits/parity
    void wakeLoop() const;                              // poke the self-pipe to break poll()
    void teardownDescriptors();

    int m_uartFd = -1;       // physical device
    int m_masterFd = -1;     // PTY master
    int m_wakeReadFd = -1;   // self-pipe read end (loop watches this)
    int m_wakeWriteFd = -1;  // self-pipe write end (close() writes here)

    QString m_slavePath;
    QString m_symlinkPath;  // created symlink, if any (unlinked on close)

    std::thread m_worker;
    std::mutex m_writeMutex;  // serializes all writes to m_uartFd / m_masterFd
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_directMode{false};
};

}  // namespace aether
