#include "core/serial/win/win_pty_proxy.hpp"

#include <QDateTime>

#include <array>

namespace aether {

namespace {

/// Build a full pipe path (\\.\pipe\<leaf>) from a user-supplied name/leaf.
QString buildPipePath(const QString &requested) {
    QString leaf = requested;
    if (leaf.isEmpty()) {
        leaf = QStringLiteral("aetherbus");
    }
    if (leaf.startsWith(QStringLiteral("\\\\.\\pipe\\"), Qt::CaseInsensitive)) {
        return leaf;
    }
    // Reduce anything path-like to a bare leaf so the pipe name stays valid.
    leaf.replace(QLatin1Char('/'), QLatin1Char('_'));
    leaf.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return QStringLiteral("\\\\.\\pipe\\") + leaf;
}

/// Prefix a COM device path with \\.\ so ports COM10+ open correctly.
QString buildComPath(const QString &device) {
    if (device.startsWith(QStringLiteral("\\\\.\\"))) {
        return device;
    }
    return QStringLiteral("\\\\.\\") + device;
}

QString lastErrorString(DWORD err) {
    LPWSTR msg = nullptr;
    const DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                     err, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    QString out =
        n > 0 && msg != nullptr ? QString::fromWCharArray(msg, static_cast<int>(n)).trimmed() : QStringLiteral("error %1").arg(err);
    if (msg != nullptr) {
        ::LocalFree(msg);
    }
    return out;
}

}  // namespace

WindowsPtyProxy::WindowsPtyProxy(PtyProxy *q) : PtyProxyImpl(q) {
    m_comReadBuf.resize(kReadBufferSize);
    m_pipeReadBuf.resize(kReadBufferSize);
}

WindowsPtyProxy::~WindowsPtyProxy() {
    WindowsPtyProxy::close();  // qualified: non-virtual dispatch is intended in a dtor
}

bool WindowsPtyProxy::makeOverlapped(Overlapped &o) {
    o.event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual-reset, unsignalled
    if (o.event == nullptr) {
        return false;
    }
    o.ov = OVERLAPPED{};
    o.ov.hEvent = o.event;
    return true;
}

void WindowsPtyProxy::closeOverlapped(Overlapped &o) {
    if (o.event != nullptr) {
        ::CloseHandle(o.event);
        o.event = nullptr;
    }
    o.ov = OVERLAPPED{};
}

bool WindowsPtyProxy::open(const SerialConfig &config) {
    if (const QString problem = config.validate(); !problem.isEmpty()) {
        emit q_ptr->errorOccurred(PtyProxy::tr("Invalid serial configuration: %1").arg(problem));
        return false;
    }
    if (m_worker.joinable()) {
        close();
    }

    m_rxBytes.store(0);
    m_txBytes.store(0);
    m_droppedBytes.store(0);
    {
        std::lock_guard<std::mutex> lk(m_writeMutex);
        m_toComQueue.clear();
        m_toPipeQueue.clear();
        m_toComQueued = 0;
        m_toPipeQueued = 0;
        m_lastStallMs = 0;
    }
    m_comWriteBuf.clear();
    m_comWriteOffset = 0;
    m_pipeWriteBuf.clear();
    m_pipeWriteOffset = 0;
    m_comEventPending = false;
    m_comWritePending = false;
    m_pipeConnectPending = false;
    m_pipeConnected = false;
    m_pipeReadPending = false;
    m_pipeWritePending = false;

    const QString comPath = buildComPath(config.device);
    m_com = ::CreateFileW(reinterpret_cast<const wchar_t *>(comPath.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED, nullptr);
    if (m_com == INVALID_HANDLE_VALUE) {
        emit q_ptr->errorOccurred(PtyProxy::tr("Cannot open %1: %2").arg(config.device, lastErrorString(::GetLastError())));
        return false;
    }
    if (!configureComState(config)) {
        teardown();
        return false;
    }

    m_directMode.store(config.directMode);
    if (m_directMode.load()) {
        m_slavePath = QStringLiteral("Direct Mode");
    } else {
        const QString pipePath = buildPipePath(config.symlinkPath);
        m_pipe = ::CreateNamedPipeW(reinterpret_cast<const wchar_t *>(pipePath.utf16()), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, kReadBufferSize, kReadBufferSize, 0, nullptr);
        if (m_pipe == INVALID_HANDLE_VALUE) {
            emit q_ptr->errorOccurred(PtyProxy::tr("Cannot create pipe %1: %2").arg(pipePath, lastErrorString(::GetLastError())));
            teardown();
            return false;
        }
        m_slavePath = pipePath;
    }

    m_stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_wakeEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset nudge
    const bool eventsOk = m_stopEvent != nullptr && m_wakeEvent != nullptr && makeOverlapped(m_ovCommEvent) &&
                          makeOverlapped(m_ovComRead) && makeOverlapped(m_ovComWrite) && makeOverlapped(m_ovPipeConnect) &&
                          makeOverlapped(m_ovPipeRead) && makeOverlapped(m_ovPipeWrite);
    if (!eventsOk) {
        emit q_ptr->errorOccurred(PtyProxy::tr("Failed to allocate synchronization objects: %1").arg(lastErrorString(::GetLastError())));
        teardown();
        return false;
    }

    m_stopRequested.store(false);
    m_running.store(true);
    m_worker = std::thread(&WindowsPtyProxy::runLoop, this);

    emit q_ptr->started(m_slavePath);
    return true;
}

bool WindowsPtyProxy::configureComState(const SerialConfig &config) {
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (::GetCommState(m_com, &dcb) == 0) {
        emit q_ptr->errorOccurred(PtyProxy::tr("GetCommState failed: %1").arg(lastErrorString(::GetLastError())));
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(config.baud);
    dcb.ByteSize = static_cast<BYTE>(config.dataBits);
    dcb.fBinary = TRUE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;

    switch (config.parity) {
        case Parity::Odd:
            dcb.Parity = ODDPARITY;
            dcb.fParity = TRUE;
            break;
        case Parity::Even:
            dcb.Parity = EVENPARITY;
            dcb.fParity = TRUE;
            break;
        case Parity::None:
            dcb.Parity = NOPARITY;
            dcb.fParity = FALSE;
            break;
    }
    dcb.StopBits = config.stopBits == 2 ? TWOSTOPBITS : ONESTOPBIT;

    // Flow control. Default the modem lines to enabled so RTS/DTR are asserted.
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    if (config.flow == FlowControl::RtsCts) {
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    } else if (config.flow == FlowControl::XonXoff) {
        dcb.fOutX = TRUE;
        dcb.fInX = TRUE;
        dcb.XonChar = 0x11;
        dcb.XoffChar = 0x13;
        dcb.XonLim = kReadBufferSize / 4;
        dcb.XoffLim = kReadBufferSize / 4;
    }

    if (::SetCommState(m_com, &dcb) == 0) {
        emit q_ptr->errorOccurred(PtyProxy::tr("SetCommState failed: %1").arg(lastErrorString(::GetLastError())));
        return false;
    }

    // Reads return immediately with whatever bytes are already buffered; writes
    // never time out. RX arrival is detected separately via WaitCommEvent.
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    if (::SetCommTimeouts(m_com, &timeouts) == 0) {
        emit q_ptr->errorOccurred(PtyProxy::tr("SetCommTimeouts failed: %1").arg(lastErrorString(::GetLastError())));
        return false;
    }
    ::SetCommMask(m_com, EV_RXCHAR);
    m_rts.store(config.flow != FlowControl::RtsCts);  // handshake mode drives RTS automatically
    m_dtr.store(true);
    return true;
}

void WindowsPtyProxy::runLoop() {
    bool deviceLost = false;

    if (!armCommEvent()) {
        deviceLost = true;
    }
    if (!m_directMode.load() && !deviceLost) {
        // Await the first client connection.
        if (::ConnectNamedPipe(m_pipe, &m_ovPipeConnect.ov) != 0) {
            onPipeConnected();
        } else {
            const DWORD err = ::GetLastError();
            if (err == ERROR_IO_PENDING) {
                m_pipeConnectPending = true;
            } else if (err == ERROR_PIPE_CONNECTED) {
                onPipeConnected();
            }
        }
    }

    while (!m_stopRequested.load() && !deviceLost) {
        std::array<HANDLE, 8> handles{};
        std::array<int, 8> action{};
        DWORD nh = 0;
        enum { A_STOP, A_WAKE, A_COMMEVT, A_COMWRITE, A_PIPECONN, A_PIPEREAD, A_PIPEWRITE };
        handles[nh] = m_stopEvent;
        action[nh] = A_STOP;
        ++nh;
        handles[nh] = m_wakeEvent;
        action[nh] = A_WAKE;
        ++nh;
        if (m_comEventPending) {
            handles[nh] = m_ovCommEvent.event;
            action[nh] = A_COMMEVT;
            ++nh;
        }
        if (m_comWritePending) {
            handles[nh] = m_ovComWrite.event;
            action[nh] = A_COMWRITE;
            ++nh;
        }
        if (!m_directMode.load()) {
            if (m_pipeConnectPending) {
                handles[nh] = m_ovPipeConnect.event;
                action[nh] = A_PIPECONN;
                ++nh;
            }
            if (m_pipeReadPending) {
                handles[nh] = m_ovPipeRead.event;
                action[nh] = A_PIPEREAD;
                ++nh;
            }
            if (m_pipeWritePending) {
                handles[nh] = m_ovPipeWrite.event;
                action[nh] = A_PIPEWRITE;
                ++nh;
            }
        }

        const DWORD w = ::WaitForMultipleObjects(nh, handles.data(), FALSE, INFINITE);
        if (w == WAIT_FAILED) {
            deviceLost = true;
            break;
        }
        const DWORD idx = w - WAIT_OBJECT_0;
        if (idx >= nh) {
            continue;
        }
        switch (action[idx]) {
            case A_STOP:
                deviceLost = false;
                m_stopRequested.store(true);
                break;
            case A_WAKE:
                onWake();
                break;
            case A_COMMEVT:
                if (!onCommEvent()) {
                    deviceLost = true;
                }
                break;
            case A_COMWRITE:
                if (!onComWriteComplete()) {
                    deviceLost = true;
                }
                break;
            case A_PIPECONN:
                onPipeConnected();
                break;
            case A_PIPEREAD:
                if (!onPipeReadComplete()) {
                    handlePipeBroken();
                }
                break;
            case A_PIPEWRITE:
                if (!onPipeWriteComplete()) {
                    handlePipeBroken();
                }
                break;
            default:
                break;
        }
    }

    m_running.store(false);
    if (deviceLost && !m_stopRequested.load()) {
        emit q_ptr->disconnected();
    }
}

bool WindowsPtyProxy::armCommEvent() {
    ::ResetEvent(m_ovCommEvent.event);
    m_commEvtMask = 0;
    if (::WaitCommEvent(m_com, &m_commEvtMask, &m_ovCommEvent.ov) != 0) {
        m_comEventPending = true;  // completed synchronously; event is signalled
        return true;
    }
    const DWORD err = ::GetLastError();
    if (err == ERROR_IO_PENDING) {
        m_comEventPending = true;
        return true;
    }
    return false;
}

bool WindowsPtyProxy::onCommEvent() {
    m_comEventPending = false;
    DWORD transferred = 0;
    if (::GetOverlappedResult(m_com, &m_ovCommEvent.ov, &transferred, FALSE) == 0) {
        return false;
    }
    if ((m_commEvtMask & EV_RXCHAR) != 0) {
        if (!drainComInput()) {
            return false;
        }
    }
    return armCommEvent();
}

bool WindowsPtyProxy::drainComInput() {
    for (;;) {
        ::ResetEvent(m_ovComRead.event);
        DWORD n = 0;
        if (::ReadFile(m_com, m_comReadBuf.data(), static_cast<DWORD>(m_comReadBuf.size()), &n, &m_ovComRead.ov) == 0) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_IO_PENDING) {
                if (::GetOverlappedResult(m_com, &m_ovComRead.ov, &n, TRUE) == 0) {
                    return false;
                }
            } else {
                return false;
            }
        }
        if (n == 0) {
            break;
        }
        QByteArray data(m_comReadBuf.data(), static_cast<int>(n));
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        m_rxBytes.fetch_add(n);
        emit q_ptr->chunkCaptured({ts, Direction::Rx, data});
        m_pcap.writePacket(ts, Direction::Rx, data);
        if (!m_directMode.load()) {
            std::lock_guard<std::mutex> lk(m_writeMutex);
            enqueue(m_toPipeQueue, m_toPipeQueued, data, Direction::Rx);
        }
    }
    if (!m_directMode.load()) {
        startPipeWrite();
    }
    return true;
}

void WindowsPtyProxy::onPipeConnected() {
    m_pipeConnectPending = false;
    m_pipeConnected = true;
    armPipeRead();
    startPipeWrite();
}

bool WindowsPtyProxy::armPipeRead() {
    if (!m_pipeConnected || m_pipeReadPending) {
        return true;
    }
    ::ResetEvent(m_ovPipeRead.event);
    DWORD n = 0;
    if (::ReadFile(m_pipe, m_pipeReadBuf.data(), static_cast<DWORD>(m_pipeReadBuf.size()), &n, &m_ovPipeRead.ov) != 0) {
        m_pipeReadPending = true;  // completed synchronously; event is signalled
        return true;
    }
    const DWORD err = ::GetLastError();
    if (err == ERROR_IO_PENDING) {
        m_pipeReadPending = true;
        return true;
    }
    handlePipeBroken();
    return false;
}

bool WindowsPtyProxy::onPipeReadComplete() {
    m_pipeReadPending = false;
    DWORD n = 0;
    if (::GetOverlappedResult(m_pipe, &m_ovPipeRead.ov, &n, FALSE) == 0) {
        return false;
    }
    if (n > 0) {
        QByteArray data(m_pipeReadBuf.data(), static_cast<int>(n));
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        m_txBytes.fetch_add(n);
        emit q_ptr->chunkCaptured({ts, Direction::Tx, data});
        m_pcap.writePacket(ts, Direction::Tx, data);
        {
            std::lock_guard<std::mutex> lk(m_writeMutex);
            enqueue(m_toComQueue, m_toComQueued, data, Direction::Tx);
        }
        startComWrite();
    }
    return armPipeRead();
}

void WindowsPtyProxy::handlePipeBroken() {
    if (m_pipe == INVALID_HANDLE_VALUE) {
        return;
    }
    m_pipeReadPending = false;
    m_pipeWritePending = false;
    m_pipeConnected = false;
    m_pipeWriteBuf.clear();
    m_pipeWriteOffset = 0;
    {
        std::lock_guard<std::mutex> lk(m_writeMutex);
        m_toPipeQueue.clear();
        m_toPipeQueued = 0;
    }
    ::DisconnectNamedPipe(m_pipe);
    // Await a fresh client so the proxy keeps running across reconnects.
    ::ResetEvent(m_ovPipeConnect.event);
    if (::ConnectNamedPipe(m_pipe, &m_ovPipeConnect.ov) != 0) {
        onPipeConnected();
        return;
    }
    const DWORD err = ::GetLastError();
    if (err == ERROR_IO_PENDING) {
        m_pipeConnectPending = true;
    } else if (err == ERROR_PIPE_CONNECTED) {
        onPipeConnected();
    }
}

void WindowsPtyProxy::onWake() {
    // A GUI-thread inject enqueued work; kick both writers.
    startComWrite();
    if (!m_directMode.load()) {
        startPipeWrite();
    }
}

void WindowsPtyProxy::enqueue(std::deque<QByteArray> &queue, std::size_t &queued, const QByteArray &data, Direction dir) {
    // Caller holds m_writeMutex.
    if (queued + static_cast<std::size_t>(data.size()) > kMaxQueueBytes) {
        const std::uint64_t total =
            m_droppedBytes.fetch_add(static_cast<std::uint64_t>(data.size())) + static_cast<std::uint64_t>(data.size());
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastStallMs >= 1000) {
            m_lastStallMs = now;
            emit q_ptr->writeStalled(dir, total);
        }
        return;
    }
    queue.push_back(data);
    queued += static_cast<std::size_t>(data.size());
}

bool WindowsPtyProxy::startComWrite() {
    if (m_comWritePending) {
        return true;
    }
    if (m_comWriteBuf.isEmpty()) {
        std::lock_guard<std::mutex> lk(m_writeMutex);
        if (m_toComQueue.empty()) {
            return true;
        }
        m_comWriteBuf = m_toComQueue.front();
        m_toComQueue.pop_front();
        m_toComQueued -= static_cast<std::size_t>(m_comWriteBuf.size());
        m_comWriteOffset = 0;
    }
    ::ResetEvent(m_ovComWrite.event);
    DWORD n = 0;
    const DWORD remaining = static_cast<DWORD>(m_comWriteBuf.size()) - static_cast<DWORD>(m_comWriteOffset);
    if (::WriteFile(m_com, m_comWriteBuf.constData() + m_comWriteOffset, remaining, &n, &m_ovComWrite.ov) == 0) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_IO_PENDING) {
            m_comWritePending = true;
            return true;
        }
        return false;
    }
    m_comWritePending = true;  // completed synchronously; event is signalled
    return true;
}

bool WindowsPtyProxy::onComWriteComplete() {
    m_comWritePending = false;
    DWORD written = 0;
    if (::GetOverlappedResult(m_com, &m_ovComWrite.ov, &written, FALSE) == 0) {
        return false;
    }
    m_comWriteOffset += written;
    if (m_comWriteOffset >= static_cast<std::size_t>(m_comWriteBuf.size())) {
        m_comWriteBuf.clear();
        m_comWriteOffset = 0;
    }
    return startComWrite();
}

bool WindowsPtyProxy::startPipeWrite() {
    if (m_pipeWritePending || !m_pipeConnected) {
        return true;
    }
    if (m_pipeWriteBuf.isEmpty()) {
        std::lock_guard<std::mutex> lk(m_writeMutex);
        if (m_toPipeQueue.empty()) {
            return true;
        }
        m_pipeWriteBuf = m_toPipeQueue.front();
        m_toPipeQueue.pop_front();
        m_toPipeQueued -= static_cast<std::size_t>(m_pipeWriteBuf.size());
        m_pipeWriteOffset = 0;
    }
    ::ResetEvent(m_ovPipeWrite.event);
    DWORD n = 0;
    const DWORD remaining = static_cast<DWORD>(m_pipeWriteBuf.size()) - static_cast<DWORD>(m_pipeWriteOffset);
    if (::WriteFile(m_pipe, m_pipeWriteBuf.constData() + m_pipeWriteOffset, remaining, &n, &m_ovPipeWrite.ov) == 0) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_IO_PENDING) {
            m_pipeWritePending = true;
            return true;
        }
        handlePipeBroken();
        return false;
    }
    m_pipeWritePending = true;  // completed synchronously; event is signalled
    return true;
}

bool WindowsPtyProxy::onPipeWriteComplete() {
    m_pipeWritePending = false;
    DWORD written = 0;
    if (::GetOverlappedResult(m_pipe, &m_ovPipeWrite.ov, &written, FALSE) == 0) {
        return false;
    }
    m_pipeWriteOffset += written;
    if (m_pipeWriteOffset >= static_cast<std::size_t>(m_pipeWriteBuf.size())) {
        m_pipeWriteBuf.clear();
        m_pipeWriteOffset = 0;
    }
    return startPipeWrite();
}

void WindowsPtyProxy::close() {
    if (m_worker.joinable()) {
        m_stopRequested.store(true);
        if (m_stopEvent != nullptr) {
            ::SetEvent(m_stopEvent);
        }
        // Unblock any in-flight overlapped I/O so the worker can exit promptly.
        if (m_com != INVALID_HANDLE_VALUE) {
            ::CancelIoEx(m_com, nullptr);
        }
        if (m_pipe != INVALID_HANDLE_VALUE) {
            ::CancelIoEx(m_pipe, nullptr);
        }
        m_worker.join();
    }
    m_running.store(false);
    teardown();
    emit q_ptr->stopped();
}

void WindowsPtyProxy::teardown() {
    if (m_pipe != INVALID_HANDLE_VALUE) {
        ::DisconnectNamedPipe(m_pipe);
        ::CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
    if (m_com != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_com);
        m_com = INVALID_HANDLE_VALUE;
    }
    closeOverlapped(m_ovCommEvent);
    closeOverlapped(m_ovComRead);
    closeOverlapped(m_ovComWrite);
    closeOverlapped(m_ovPipeConnect);
    closeOverlapped(m_ovPipeRead);
    closeOverlapped(m_ovPipeWrite);
    if (m_stopEvent != nullptr) {
        ::CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
    if (m_wakeEvent != nullptr) {
        ::CloseHandle(m_wakeEvent);
        m_wakeEvent = nullptr;
    }
    m_slavePath.clear();
    m_pipeConnected = false;
}

bool WindowsPtyProxy::injectToDevice(const QByteArray &bytes) {
    if (m_com == INVALID_HANDLE_VALUE || bytes.isEmpty()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(m_writeMutex);
        enqueue(m_toComQueue, m_toComQueued, bytes, Direction::Tx);
    }
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    emit q_ptr->chunkCaptured({ts, Direction::Tx, bytes});
    m_pcap.writePacket(ts, Direction::Tx, bytes);
    if (m_wakeEvent != nullptr) {
        ::SetEvent(m_wakeEvent);
    }
    return true;
}

bool WindowsPtyProxy::injectToApp(const QByteArray &bytes) {
    if (m_directMode.load() || m_pipe == INVALID_HANDLE_VALUE || bytes.isEmpty()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(m_writeMutex);
        enqueue(m_toPipeQueue, m_toPipeQueued, bytes, Direction::Rx);
    }
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    emit q_ptr->chunkCaptured({ts, Direction::Rx, bytes});
    m_pcap.writePacket(ts, Direction::Rx, bytes);
    if (m_wakeEvent != nullptr) {
        ::SetEvent(m_wakeEvent);
    }
    return true;
}

bool WindowsPtyProxy::isRunning() const {
    return m_running.load();
}

QString WindowsPtyProxy::slavePath() const {
    return m_slavePath;
}

PtyProxy::Stats WindowsPtyProxy::stats() const {
    PtyProxy::Stats s;
    s.rx = m_rxBytes.load();
    s.tx = m_txBytes.load();
    s.dropped = m_droppedBytes.load();
    return s;
}

bool WindowsPtyProxy::startCapture(const QString &path) {
    QString error;
    if (!m_pcap.open(path, &error)) {
        emit q_ptr->errorOccurred(PtyProxy::tr("Cannot open capture file %1: %2").arg(path, error));
        return false;
    }
    return true;
}

void WindowsPtyProxy::stopCapture() {
    m_pcap.close();
}

bool WindowsPtyProxy::isCapturing() const {
    return m_pcap.isOpen();
}

PtyProxy::ModemLines WindowsPtyProxy::modemLines() const {
    PtyProxy::ModemLines lines;
    if (m_com == INVALID_HANDLE_VALUE) {
        return lines;
    }
    DWORD status = 0;
    if (::GetCommModemStatus(m_com, &status) != 0) {
        lines.cts = (status & MS_CTS_ON) != 0;
        lines.dsr = (status & MS_DSR_ON) != 0;
        lines.dcd = (status & MS_RLSD_ON) != 0;
        lines.ri = (status & MS_RING_ON) != 0;
    }
    // Windows exposes no "read RTS/DTR"; report the state we last drove.
    lines.rts = m_rts.load();
    lines.dtr = m_dtr.load();
    return lines;
}

bool WindowsPtyProxy::setRts(bool on) {
    if (m_com == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (::EscapeCommFunction(m_com, on ? SETRTS : CLRRTS) == 0) {
        return false;
    }
    m_rts.store(on);
    return true;
}

bool WindowsPtyProxy::setDtr(bool on) {
    if (m_com == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (::EscapeCommFunction(m_com, on ? SETDTR : CLRDTR) == 0) {
        return false;
    }
    m_dtr.store(on);
    return true;
}

bool WindowsPtyProxy::sendBreak() {
    if (m_com == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (::SetCommBreak(m_com) == 0) {
        return false;
    }
    ::Sleep(1);
    return ::ClearCommBreak(m_com) != 0;
}

}  // namespace aether
