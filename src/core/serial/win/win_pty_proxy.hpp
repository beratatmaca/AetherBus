#pragma once
#include "core/common/pcap_writer.hpp"
#include "core/serial/pty_proxy_impl.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

namespace aether {

/**
 * @brief Windows serial interceptor exposing the virtual side as a named pipe.
 *
 * Windows has no pseudo-terminal and no driver-free way to publish a real
 * @c COMx device to third-party applications (that requires a signed kernel
 * driver such as com0com). This proxy instead opens and configures the physical
 * COM port and exposes the "application" side as a named pipe
 * (@c \\.\pipe\aetherbus-*). Bytes read from the COM port are tagged
 * @ref Direction::Rx and written to the pipe; bytes the connected client writes
 * to the pipe are tagged @ref Direction::Tx and written to the COM port. It is
 * fully functional for capture, statistics, injection and modem-line control,
 * and for any client that speaks the named pipe; it is not automatically visible
 * as a @c COMx port.
 *
 * All physical/pipe I/O is overlapped and multiplexed on a single worker thread
 * via @c WaitForMultipleObjects, mirroring the POSIX @c poll() proxy.
 */
class WindowsPtyProxy : public PtyProxyImpl {
public:
    explicit WindowsPtyProxy(PtyProxy *q);
    ~WindowsPtyProxy() override;

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
    struct Overlapped {
        OVERLAPPED ov{};
        HANDLE event = nullptr;
    };

    void runLoop();
    bool configureComState(const SerialConfig &config);

    // Worker-thread helpers (each returns false on a fatal handle error).
    bool armCommEvent();
    bool onCommEvent();
    bool drainComInput();
    bool onComWriteComplete();
    void onPipeConnected();
    bool armPipeRead();
    bool onPipeReadComplete();
    bool onPipeWriteComplete();
    void handlePipeBroken();
    void onWake();

    // Queue a chunk for a target and (worker context) kick the write.
    void enqueue(std::deque<QByteArray> &queue, std::size_t &queued, const QByteArray &data, Direction dir);
    bool startComWrite();   ///< Begin one overlapped write to the COM port.
    bool startPipeWrite();  ///< Begin one overlapped write to the pipe.

    void teardown();

    static bool makeOverlapped(Overlapped &o);
    static void closeOverlapped(Overlapped &o);

    static constexpr std::size_t kMaxQueueBytes = std::size_t{8} * 1024 * 1024;
    static constexpr int kReadBufferSize = 4096;

    HANDLE m_com = INVALID_HANDLE_VALUE;
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    HANDLE m_stopEvent = nullptr;
    HANDLE m_wakeEvent = nullptr;

    Overlapped m_ovCommEvent;
    Overlapped m_ovComRead;
    Overlapped m_ovComWrite;
    Overlapped m_ovPipeConnect;
    Overlapped m_ovPipeRead;
    Overlapped m_ovPipeWrite;

    DWORD m_commEvtMask = 0;
    std::vector<char> m_comReadBuf;
    std::vector<char> m_pipeReadBuf;

    bool m_comEventPending = false;
    bool m_comWritePending = false;
    bool m_pipeConnectPending = false;
    bool m_pipeConnected = false;
    bool m_pipeReadPending = false;
    bool m_pipeWritePending = false;

    // In-flight write chunk + offset (bytes already written) per target.
    QByteArray m_comWriteBuf;
    std::size_t m_comWriteOffset = 0;
    QByteArray m_pipeWriteBuf;
    std::size_t m_pipeWriteOffset = 0;

    QString m_slavePath;

    std::thread m_worker;
    std::mutex m_writeMutex;
    std::deque<QByteArray> m_toComQueue;
    std::deque<QByteArray> m_toPipeQueue;
    std::size_t m_toComQueued = 0;
    std::size_t m_toPipeQueued = 0;
    qint64 m_lastStallMs = 0;

    std::atomic<std::uint64_t> m_rxBytes{0};
    std::atomic<std::uint64_t> m_txBytes{0};
    std::atomic<std::uint64_t> m_droppedBytes{0};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_directMode{false};

    std::atomic<bool> m_rts{true};
    std::atomic<bool> m_dtr{true};

    PcapWriter m_pcap;
};

}  // namespace aether
