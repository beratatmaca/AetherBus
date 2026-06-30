// Interceptty-style man-in-the-middle proxy.
//
// PtyProxy opens a physical UART and a kernel pseudo-terminal master/slave
// pair, then runs a background poll() loop that shuttles bytes between them
// while emitting a direction-tagged copy of every chunk for the GUI.
//
// Threading contract: open()/close()/inject*() are called from the GUI thread.
// chunkCaptured() is emitted from the worker thread and is delivered to GUI
// slots via a queued connection (CapturedChunk is a registered metatype).
//
// This class is POSIX-only and is compiled exclusively on UNIX platforms.
#pragma once

#include "core/serial_types.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <thread>

namespace aether {

class PtyProxy : public QObject {
    Q_OBJECT

public:
    explicit PtyProxy(QObject *parent = nullptr);
    ~PtyProxy() override;

    PtyProxy(const PtyProxy &) = delete;
    PtyProxy &operator=(const PtyProxy &) = delete;

    /// Open the physical device and a fresh PTY pair, then start the worker
    /// loop. Returns false (and emits errorOccurred) on any failure; on
    /// success the slave path is available via slavePath().
    bool open(const SerialConfig &config);

    /// Stop the worker loop and release every descriptor and symlink. Safe to
    /// call repeatedly and from the destructor.
    void close();

    [[nodiscard]] bool isRunning() const { return m_running.load(); }

    /// Kernel-assigned slave PTY path (e.g. "/dev/pts/5"), valid after open().
    [[nodiscard]] QString slavePath() const { return m_slavePath; }

    /// Write raw bytes straight to the physical UART (acts as the host).
    bool injectToDevice(const QByteArray &bytes);

    /// Write raw bytes to the PTY master, so the target app sees them as Rx.
    bool injectToApp(const QByteArray &bytes);

    /// Modem control line states, as reported by/applied to the physical UART.
    struct ModemLines {
        bool cts = false;  ///< Clear To Send (input)
        bool dsr = false;  ///< Data Set Ready (input)
        bool dcd = false;  ///< Data Carrier Detect (input)
        bool ri = false;   ///< Ring Indicator (input)
        bool rts = false;  ///< Request To Send (output)
        bool dtr = false;  ///< Data Terminal Ready (output)
    };

    /// Read the current modem control lines from the physical UART.
    [[nodiscard]] ModemLines modemLines() const;

    /// Drive the RTS / DTR output lines on the physical UART.
    [[nodiscard]] bool setRts(bool on) const;
    [[nodiscard]] bool setDtr(bool on) const;

    /// Send a serial break condition on the physical UART.
    [[nodiscard]] bool sendBreak() const;

signals:
    void chunkCaptured(const aether::CapturedChunk &chunk);
    void errorOccurred(const QString &message);
    void started(const QString &slavePath);
    void stopped();

    /// Emitted (queued) when the multiplexing loop aborts unexpectedly —
    /// e.g. the device was unplugged. Drives optional GUI auto-reconnect.
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
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_directMode{false};
};

}  // namespace aether
