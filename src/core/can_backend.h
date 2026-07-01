/**
 * @file can_backend.h
 * @brief SocketCAN capture/transmit backend implementing @ref IBusBackend.
 *
 * Linux-only under the hood; the class is declared on every platform so the GUI
 * can reference it unconditionally and gate on @ref isSupported(). On non-Linux
 * builds every operation is a safe no-op that reports the transport as
 * unsupported.
 *
 * Threading mirrors the serial PTY proxy: a background worker runs a @c poll()
 * loop over the CAN socket plus a self-pipe wake fd, and emits @c chunkCaptured
 * across a queued connection to the owning (GUI) thread.
 */
#pragma once

#include "core/can_types.h"
#include "core/i_bus_backend.h"

#include <QStringList>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

namespace aether {

class CanBackend : public IBusBackend {
    Q_OBJECT

public:
    explicit CanBackend(QObject *parent = nullptr);
    ~CanBackend() override;

    /// Open and bind a raw CAN socket on @p config.iface. @return true on success.
    bool open(const CanConfig &config);
    void close() override;
    [[nodiscard]] bool isRunning() const override;

    /// Interface this backend is bound to (empty when closed).
    [[nodiscard]] QString iface() const;

    /**
     * @brief Transmit a single frame onto the bus.
     * @param id      Identifier with flag bits already masked off.
     * @param flags   Bitmask of @ref FrameFlag (EFF/RTR/FD/BRS/ESI honoured).
     * @param payload Up to 8 bytes (classic) or 64 bytes (CAN-FD).
     * @return true when the frame was queued for transmission.
     */
    bool sendFrame(quint32 id, quint16 flags, const QByteArray &payload);

    struct Stats {
        quint64 rxFrames = 0;
        quint64 txFrames = 0;
        quint64 dropped = 0;
    };
    [[nodiscard]] Stats stats() const;

    // --- Static helpers, safe to call on any platform ---

    /// @return true when SocketCAN is available on this build/platform.
    [[nodiscard]] static bool isSupported();

    /// @return names of CAN interfaces currently present (empty if none/unsupported).
    [[nodiscard]] static QStringList listInterfaces();

    /// @return the configured bit rate of @p iface, or -1 if unknown/unsupported.
    [[nodiscard]] static int queryBitrate(const QString &iface);

private:
    void runLoop();
    void wakeLoop();
    void teardown();

    int m_sockFd = -1;
    int m_wakeReadFd = -1;
    int m_wakeWriteFd = -1;
    QString m_iface;
    bool m_fdMode = false;

    std::thread m_worker;
    std::mutex m_txMutex;
    std::deque<QByteArray> m_txQueue;  ///< raw can(fd)_frame blobs awaiting write

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_ioFailed{false};

    std::atomic<std::uint64_t> m_rxFrames{0};
    std::atomic<std::uint64_t> m_txFrames{0};
    std::atomic<std::uint64_t> m_dropped{0};
};

}  // namespace aether
