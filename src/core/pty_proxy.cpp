#include "core/pty_proxy.h"

#include "core/linux_baud.h"
#include "core/signal_cleanup.h"

#include <QDateTime>
#include <QFile>

#include <array>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace aether {

namespace {

// Map a numeric baud rate to its termios speed_t constant. Returns false for
// unsupported rates so the caller can report a clean error.
bool baudToSpeed(int baud, speed_t &speed) {
    switch (baud) {
        case 9600:
            speed = B9600;
            return true;
        case 19200:
            speed = B19200;
            return true;
        case 38400:
            speed = B38400;
            return true;
        case 57600:
            speed = B57600;
            return true;
        case 115200:
            speed = B115200;
            return true;
        case 230400:
            speed = B230400;
            return true;
        case 460800:
            speed = B460800;
            return true;
        case 921600:
            speed = B921600;
            return true;
        default:
            return false;
    }
}

// Reverse of baudToSpeed(): map a termios speed constant back to a numeric rate.
// Returns 0 for non-standard rates (e.g. those set via the termios2 BOTHER path).
int speedToBaud(speed_t speed) {
    switch (speed) {
        case B9600:
            return 9600;
        case B19200:
            return 19200;
        case B38400:
            return 38400;
        case B57600:
            return 57600;
        case B115200:
            return 115200;
        case B230400:
            return 230400;
        case B460800:
            return 460800;
        case B921600:
            return 921600;
        default:
            return 0;
    }
}

// Little-/big-endian append helpers for building pcap records by hand.
void appendLe16(QByteArray &out, quint16 v) {
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
}
void appendLe32(QByteArray &out, quint32 v) {
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>((v >> 16) & 0xFF));
    out.append(static_cast<char>((v >> 24) & 0xFF));
}
void appendBe32(QByteArray &out, quint32 v) {
    out.append(static_cast<char>((v >> 24) & 0xFF));
    out.append(static_cast<char>((v >> 16) & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>(v & 0xFF));
}

// pcap LINKTYPE_RTAC_SERIAL: a 12-byte pseudo-header precedes each payload.
constexpr quint32 kLinkTypeRtacSerial = 250;
constexpr int kRtacHeaderLen = 12;

}  // namespace

PtyProxy::PtyProxy(QObject *parent) : QObject(parent) {
    qRegisterMetaType<aether::CapturedChunk>("aether::CapturedChunk");
    qRegisterMetaType<aether::Direction>("aether::Direction");
    qRegisterMetaType<aether::Parity>("aether::Parity");
}

PtyProxy::~PtyProxy() {
    close();
}

bool PtyProxy::open(const SerialConfig &config) {
    // Reject out-of-range line settings up front so termios never sees a value
    // it would silently coerce (e.g. an unexpected dataBits falling to CS8).
    if (const QString problem = config.validate(); !problem.isEmpty()) {
        emit errorOccurred(tr("Invalid serial configuration: %1").arg(problem));
        return false;
    }

    // Reclaim any prior worker — including one that exited on its own after a
    // device drop (joinable but with m_running already false). This also frees
    // descriptors/symlink left open by the self-terminating loop.
    if (m_worker.joinable()) {
        close();
    }

    // Reset per-session state: counters, queued writes and the fatal-write flag.
    m_rxBytes.store(0);
    m_txBytes.store(0);
    m_droppedBytes.store(0);
    m_ioFailed.store(false);
    // Seed the mirror cache with the initial config so only a genuine change
    // by the target app triggers a lineReconfigured() emission.
    m_lastBaud = config.baud;
    m_lastDataBits = config.dataBits;
    m_lastParity = config.parity;
    m_lastStopBits = config.stopBits;
    {
        std::lock_guard<std::mutex> lk(m_writeMutex);
        m_uartOut = OutQueue{};
        m_masterOut = OutQueue{};
        m_lastStallMs = 0;
    }

    // 1. Open the physical UART in raw, non-blocking mode.
    m_uartFd = ::open(config.device.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_uartFd < 0) {
        emit errorOccurred(tr("Cannot open %1: %2").arg(config.device, QString::fromLocal8Bit(strerror(errno))));
        return false;
    }
    if (!configureTermios(config)) {
        teardownDescriptors();
        return false;
    }

    // 2. Request a new master/slave pseudo-terminal pair from the kernel if not in direct mode.
    m_directMode.store(config.directMode);
    if (m_directMode.load()) {
        m_slavePath = QStringLiteral("Direct Mode");
        m_masterFd = -1;
    } else {
        m_masterFd = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (m_masterFd < 0 || ::grantpt(m_masterFd) != 0 || ::unlockpt(m_masterFd) != 0) {
            emit errorOccurred(tr("Failed to allocate pseudo-terminal: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            teardownDescriptors();
            return false;
        }
        const char *slave = ::ptsname(m_masterFd);
        if (slave == nullptr) {
            emit errorOccurred(tr("ptsname() failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            teardownDescriptors();
            return false;
        }
        m_slavePath = QString::fromLocal8Bit(slave);

        // Make the master non-blocking too so poll() drives all reads.
        const int flags = ::fcntl(m_masterFd, F_GETFL, 0);
        ::fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);

        // Enable PTY packet mode so the target application's tcsetattr() calls on
        // the slave surface as control packets on the master. Unlike BSD, Linux
        // never sets TIOCPKT_IOCTL; a line-discipline change instead arrives as a
        // flush / NOSTOP / DOSTOP control byte, which we use to re-mirror settings.
        int packetMode = 1;
        ::ioctl(m_masterFd, TIOCPKT, &packetMode);

        // 3. Optionally expose a stable symlink to the slave node.
        if (!config.symlinkPath.isEmpty()) {
            ::unlink(config.symlinkPath.toLocal8Bit().constData());
            if (::symlink(slave, config.symlinkPath.toLocal8Bit().constData()) == 0) {
                m_symlinkPath = config.symlinkPath;
            } else {
                emit errorOccurred(tr("Could not create symlink %1: %2").arg(config.symlinkPath, QString::fromLocal8Bit(strerror(errno))));
                // Non-fatal: the slave path is still usable directly.
            }
        }
    }

    // 4. Self-pipe so close() can interrupt a blocked poll() promptly.
    std::array<int, 2> pipeFds{-1, -1};
    if (::pipe(pipeFds.data()) != 0) {
        emit errorOccurred(tr("pipe() failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        teardownDescriptors();
        return false;
    }
    m_wakeReadFd = pipeFds[0];
    m_wakeWriteFd = pipeFds[1];
    ::fcntl(m_wakeReadFd, F_SETFL, O_NONBLOCK);

    // Register for signal-safe cleanup so a sudden exit still unlinks the symlink
    // and releases the device (see signal_cleanup.h / installSignalHandlers()).
    m_cleanupSlot = registerCleanup(m_symlinkPath.isEmpty() ? nullptr : m_symlinkPath.toLocal8Bit().constData(), m_uartFd, m_masterFd);

    // 5. Launch the multiplexing loop.
    m_stopRequested.store(false);
    m_running.store(true);
    m_worker = std::thread(&PtyProxy::runLoop, this);

    emit started(m_slavePath);
    return true;
}

bool PtyProxy::configureTermios(const SerialConfig &config) {
    // Standard rates map straight to a Bxxxx constant; non-standard rates are
    // applied afterwards via the Linux termios2 path (see below).
    speed_t speed = B115200;
    const bool standardBaud = baudToSpeed(config.baud, speed);

    termios tio{};
    if (::tcgetattr(m_uartFd, &tio) != 0) {
        emit errorOccurred(tr("tcgetattr failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return false;
    }

    ::cfmakeraw(&tio);
    ::cfsetispeed(&tio, speed);
    ::cfsetospeed(&tio, speed);

    tio.c_cflag &= ~static_cast<tcflag_t>(CSIZE);
    switch (config.dataBits) {
        case 5:
            tio.c_cflag |= CS5;
            break;
        case 6:
            tio.c_cflag |= CS6;
            break;
        case 7:
            tio.c_cflag |= CS7;
            break;
        default:
            tio.c_cflag |= CS8;
            break;
    }

    switch (config.parity) {
        case Parity::None:
            tio.c_cflag &= ~static_cast<tcflag_t>(PARENB);
            break;
        case Parity::Odd:
            tio.c_cflag |= (PARENB | PARODD);
            break;
        case Parity::Even:
            tio.c_cflag |= PARENB;
            tio.c_cflag &= ~static_cast<tcflag_t>(PARODD);
            break;
    }

    if (config.stopBits == 2) {
        tio.c_cflag |= CSTOPB;
    } else {
        tio.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);
    }

    tio.c_cflag |= (CLOCAL | CREAD);

    // Flow control / handshake.
    tio.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
    tio.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY);
    if (config.flow == FlowControl::RtsCts) {
        tio.c_cflag |= CRTSCTS;
    } else if (config.flow == FlowControl::XonXoff) {
        tio.c_iflag |= (IXON | IXOFF);
    }

    if (::tcsetattr(m_uartFd, TCSANOW, &tio) != 0) {
        emit errorOccurred(tr("tcsetattr failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return false;
    }

    // Non-standard baud: override the speed exactly via Linux termios2. This
    // preserves the cflags just set by tcsetattr().
    if (!standardBaud) {
        if (!setCustomBaud(m_uartFd, config.baud)) {
            emit errorOccurred(tr("Unsupported baud rate: %1").arg(config.baud));
            return false;
        }
    }
    return true;
}

void PtyProxy::runLoop() {
    std::vector<char> buf(4096);
    bool deviceLost = false;

    while (!m_stopRequested.load()) {
        // A fatal write (from this loop or an inject on the GUI thread) means the
        // link is gone; surface it as a disconnect.
        if (m_ioFailed.load()) {
            deviceLost = true;
            break;
        }

        const bool direct = m_directMode.load();

        // Watch POLLOUT only while a destination still has buffered bytes, so an
        // idle proxy blocks cleanly in poll() instead of busy-looping.
        short uartEvents = POLLIN;
        short masterEvents = POLLIN;
        {
            std::lock_guard<std::mutex> lk(m_writeMutex);
            if (m_uartOut.queued > 0) {
                uartEvents |= POLLOUT;
            }
            if (!direct && m_masterOut.queued > 0) {
                masterEvents |= POLLOUT;
            }
        }

        std::array<pollfd, 3> fds{};
        int masterIdx = -1;
        int wakeIdx = 1;
        fds[0] = {m_uartFd, uartEvents, 0};
        int nfds = 0;
        if (direct) {
            fds[1] = {m_wakeReadFd, POLLIN, 0};
            nfds = 2;
        } else {
            fds[1] = {m_masterFd, masterEvents, 0};
            masterIdx = 1;
            fds[2] = {m_wakeReadFd, POLLIN, 0};
            wakeIdx = 2;
            nfds = 3;
        }

        const int ready = ::poll(fds.data(), nfds, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            deviceLost = true;
            break;
        }

        // Wake pipe: drain and re-check the stop flag / newly queued writes.
        if ((fds[wakeIdx].revents & POLLIN) != 0) {
            while (::read(m_wakeReadFd, buf.data(), buf.size()) > 0) {
            }
            continue;
        }

        // Descriptor A: physical UART. Flush pending Tx first, then read new Rx. A
        // read of 0 or a hard error here means the device went away (unplugged).
        const short uartRev = fds[0].revents;
        if ((uartRev & POLLOUT) != 0) {
            std::lock_guard<std::mutex> lk(m_writeMutex);
            if (!drainLocked(m_uartFd, m_uartOut)) {
                deviceLost = true;
                break;
            }
        }
        if ((uartRev & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0) {
            const ssize_t n = ::read(m_uartFd, buf.data(), buf.size());
            if (n > 0) {
                QByteArray data(buf.data(), static_cast<int>(n));
                const qint64 ts = QDateTime::currentMSecsSinceEpoch();
                m_rxBytes.fetch_add(static_cast<std::uint64_t>(n));
                emit chunkCaptured({ts, Direction::Rx, data});
                writePcapPacket(ts, Direction::Rx, data);
                if (!direct && m_masterFd >= 0) {
                    forward(m_masterFd, m_masterOut, Direction::Rx, data);
                }
            } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                deviceLost = true;
                break;
            }
        }

        // Descriptor B: PTY master (proxy mode only). In packet mode every read is
        // prefixed by a control byte; a TIOCPKT_IOCTL packet means the target app
        // changed the slave line settings, which we mirror onto the physical UART.
        if (!direct && masterIdx >= 0) {
            const short masterRev = fds[masterIdx].revents;
            if ((masterRev & POLLOUT) != 0) {
                std::lock_guard<std::mutex> lk(m_writeMutex);
                if (!drainLocked(m_masterFd, m_masterOut)) {
                    deviceLost = true;
                    break;
                }
            }
            if ((masterRev & (POLLIN | POLLHUP)) != 0) {
                const ssize_t n = ::read(m_masterFd, buf.data(), buf.size());
                if (n > 0) {
                    const auto ctrl = static_cast<unsigned char>(buf[0]);
                    if (ctrl == TIOCPKT_DATA) {
                        if (n > 1) {
                            QByteArray data(buf.data() + 1, static_cast<int>(n - 1));
                            const qint64 ts = QDateTime::currentMSecsSinceEpoch();
                            m_txBytes.fetch_add(static_cast<std::uint64_t>(n - 1));
                            emit chunkCaptured({ts, Direction::Tx, data});
                            writePcapPacket(ts, Direction::Tx, data);
                            forward(m_uartFd, m_uartOut, Direction::Tx, data);
                        }
                    } else {
                        // Control packet. A flush or flow-mode (NOSTOP/DOSTOP)
                        // change signals the app reconfigured the line; re-mirror.
                        // Runtime XON/XOFF (STOP/START alone) is not a config change.
                        constexpr unsigned char kReconfigBits =
                            TIOCPKT_IOCTL | TIOCPKT_NOSTOP | TIOCPKT_DOSTOP | TIOCPKT_FLUSHREAD | TIOCPKT_FLUSHWRITE;
                        if ((ctrl & kReconfigBits) != 0) {
                            mirrorSlaveTermios();
                        }
                    }
                }
            }
        }
    }

    m_running.store(false);
    if (deviceLost && !m_stopRequested.load()) {
        emit disconnected();
    }
}

bool PtyProxy::drainLocked(int fd, OutQueue &queue) {
    while (!queue.chunks.empty()) {
        const QByteArray &front = queue.chunks.front();
        const char *ptr = front.constData() + queue.frontOffset;
        const std::size_t remaining = static_cast<std::size_t>(front.size()) - queue.frontOffset;
        const ssize_t n = ::write(fd, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;  // peer is full; retry on the next POLLOUT
            }
            return false;  // fatal (EPIPE/EBADF/...)
        }
        queue.frontOffset += static_cast<std::size_t>(n);
        queue.queued -= static_cast<std::size_t>(n);
        if (queue.frontOffset >= static_cast<std::size_t>(front.size())) {
            queue.chunks.pop_front();
            queue.frontOffset = 0;
        }
    }
    return true;
}

bool PtyProxy::forward(int fd, OutQueue &queue, Direction dir, const QByteArray &data) {
    if (data.isEmpty()) {
        return true;
    }
    bool dropped = false;
    bool fatal = false;
    bool emitStall = false;
    {
        std::lock_guard<std::mutex> lk(m_writeMutex);
        if (queue.queued + static_cast<std::size_t>(data.size()) > kMaxQueueBytes) {
            dropped = true;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastStallMs >= 1000) {
                m_lastStallMs = now;
                emitStall = true;
            }
        } else {
            queue.chunks.push_back(data);
            queue.queued += static_cast<std::size_t>(data.size());
            fatal = !drainLocked(fd, queue);
        }
    }
    quint64 droppedTotal = 0;
    if (dropped) {
        droppedTotal = m_droppedBytes.fetch_add(static_cast<std::uint64_t>(data.size())) + static_cast<std::uint64_t>(data.size());
    }
    if (emitStall) {
        emit writeStalled(dir, droppedTotal);
    }
    if (fatal) {
        m_ioFailed.store(true);
        wakeLoop();
    }
    return !dropped && !fatal;
}

void PtyProxy::mirrorSlaveTermios() {
    if (m_uartFd < 0 || m_masterFd < 0) {
        return;
    }
    // On Linux the termios ioctls on the master operate on the pty's (slave's)
    // settings, so tcgetattr(master) reads what the target application just set.
    termios slaveTio{};
    if (::tcgetattr(m_masterFd, &slaveTio) != 0) {
        return;
    }
    termios uartTio{};
    if (::tcgetattr(m_uartFd, &uartTio) != 0) {
        return;
    }

    // Copy only the line parameters; keep the physical port otherwise raw.
    ::cfsetispeed(&uartTio, ::cfgetispeed(&slaveTio));
    ::cfsetospeed(&uartTio, ::cfgetospeed(&slaveTio));
    const tcflag_t cmask = CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS;
    uartTio.c_cflag = (uartTio.c_cflag & ~cmask) | (slaveTio.c_cflag & cmask);
    const tcflag_t imask = IXON | IXOFF | IXANY;
    uartTio.c_iflag = (uartTio.c_iflag & ~imask) | (slaveTio.c_iflag & imask);

    if (::tcsetattr(m_uartFd, TCSANOW, &uartTio) != 0) {
        emit errorOccurred(tr("Failed to mirror line settings to the device: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

    // Decode the new parameters. Custom (BOTHER) rates reverse-map to 0 and are
    // left for the UI to display as "custom".
    const int baud = speedToBaud(::cfgetospeed(&slaveTio));
    int dataBits = 8;
    switch (uartTio.c_cflag & CSIZE) {
        case CS5:
            dataBits = 5;
            break;
        case CS6:
            dataBits = 6;
            break;
        case CS7:
            dataBits = 7;
            break;
        default:
            dataBits = 8;
            break;
    }
    Parity parity = Parity::None;
    if ((uartTio.c_cflag & PARENB) != 0) {
        parity = (uartTio.c_cflag & PARODD) != 0 ? Parity::Odd : Parity::Even;
    }
    const int stopBits = (uartTio.c_cflag & CSTOPB) != 0 ? 2 : 1;

    // Only report an actual change. Linux emits a control packet on any
    // discipline touch (including runtime flow-mode flips), so suppress
    // no-op re-mirrors to avoid spurious GUI churn.
    if (baud == m_lastBaud && dataBits == m_lastDataBits && parity == m_lastParity && stopBits == m_lastStopBits) {
        return;
    }
    m_lastBaud = baud;
    m_lastDataBits = dataBits;
    m_lastParity = parity;
    m_lastStopBits = stopBits;
    emit lineReconfigured(baud, dataBits, parity, stopBits);
}

void PtyProxy::wakeLoop() const {
    if (m_wakeWriteFd >= 0) {
        const char byte = 1;
        ssize_t rc = ::write(m_wakeWriteFd, &byte, 1);
        (void)rc;
    }
}

void PtyProxy::close() {
    if (m_worker.joinable()) {
        m_stopRequested.store(true);
        wakeLoop();
        m_worker.join();
    }
    m_running.store(false);
    teardownDescriptors();
    emit stopped();
}

void PtyProxy::teardownDescriptors() {
    // Stop tracking before we release: the emergency handler must not act on
    // descriptors/symlinks this normal path is about to reclaim.
    releaseCleanup(m_cleanupSlot);
    m_cleanupSlot = -1;

    if (!m_symlinkPath.isEmpty()) {
        ::unlink(m_symlinkPath.toLocal8Bit().constData());
        m_symlinkPath.clear();
    }
    for (int *fd : {&m_uartFd, &m_masterFd, &m_wakeReadFd, &m_wakeWriteFd}) {
        if (*fd >= 0) {
            ::close(*fd);
            *fd = -1;
        }
    }
    m_slavePath.clear();
}

bool PtyProxy::injectToDevice(const QByteArray &bytes) {
    if (m_uartFd < 0 || bytes.isEmpty()) {
        return false;
    }
    const bool ok = forward(m_uartFd, m_uartOut, Direction::Tx, bytes);
    if (ok) {
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        emit chunkCaptured({ts, Direction::Tx, bytes});
        writePcapPacket(ts, Direction::Tx, bytes);
    }
    wakeLoop();  // re-poll for POLLOUT if the write had to be buffered
    return ok;
}

bool PtyProxy::injectToApp(const QByteArray &bytes) {
    if (m_masterFd < 0 || bytes.isEmpty()) {
        return false;
    }
    const bool ok = forward(m_masterFd, m_masterOut, Direction::Rx, bytes);
    if (ok) {
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        emit chunkCaptured({ts, Direction::Rx, bytes});
        writePcapPacket(ts, Direction::Rx, bytes);
    }
    wakeLoop();
    return ok;
}

PtyProxy::Stats PtyProxy::stats() const {
    Stats s;
    s.rx = m_rxBytes.load();
    s.tx = m_txBytes.load();
    s.dropped = m_droppedBytes.load();
    return s;
}

bool PtyProxy::startCapture(const QString &path) {
    std::lock_guard<std::mutex> lk(m_captureMutex);
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(tr("Cannot open capture file %1: %2").arg(path, file->errorString()));
        return false;
    }
    // pcap global header (little-endian), network = LINKTYPE_RTAC_SERIAL.
    QByteArray header;
    appendLe32(header, 0xa1b2c3d4U);  // magic
    appendLe16(header, 2);            // version major
    appendLe16(header, 4);            // version minor
    appendLe32(header, 0);            // thiszone
    appendLe32(header, 0);            // sigfigs
    appendLe32(header, 262144);       // snaplen
    appendLe32(header, kLinkTypeRtacSerial);
    file->write(header);
    m_captureFile = std::move(file);
    return true;
}

void PtyProxy::stopCapture() {
    std::lock_guard<std::mutex> lk(m_captureMutex);
    m_captureFile.reset();  // QFile dtor flushes and closes
}

bool PtyProxy::isCapturing() const {
    std::lock_guard<std::mutex> lk(m_captureMutex);
    return m_captureFile != nullptr;
}

void PtyProxy::writePcapPacket(qint64 timestampMs, Direction dir, const QByteArray &data) {
    std::lock_guard<std::mutex> lk(m_captureMutex);
    if (!m_captureFile) {
        return;
    }
    const auto sec = static_cast<quint32>(timestampMs / 1000);
    const auto usec = static_cast<quint32>((timestampMs % 1000) * 1000);
    const auto caplen = static_cast<quint32>(kRtacHeaderLen + data.size());

    QByteArray record;
    // pcap record header (little-endian).
    appendLe32(record, sec);
    appendLe32(record, usec);
    appendLe32(record, caplen);  // incl_len
    appendLe32(record, caplen);  // orig_len
    // RTAC serial pseudo-header: 4B sec + 4B usec (big-endian) + event + ctrl + 2B footer.
    appendBe32(record, sec);
    appendBe32(record, usec);
    record.append(static_cast<char>(dir == Direction::Tx ? 0x01 : 0x02));  // DATA_TX/RX_START
    record.append(static_cast<char>(0x00));                                // control lines
    record.append(static_cast<char>(0x00));                                // footer (reserved)
    record.append(static_cast<char>(0x00));
    record.append(data);
    m_captureFile->write(record);
}

PtyProxy::ModemLines PtyProxy::modemLines() const {
    ModemLines lines;
    if (m_uartFd < 0) {
        return lines;
    }
    int status = 0;
    if (::ioctl(m_uartFd, TIOCMGET, &status) != 0) {
        return lines;
    }
    lines.cts = (status & TIOCM_CTS) != 0;
    lines.dsr = (status & TIOCM_DSR) != 0;
    lines.dcd = (status & TIOCM_CAR) != 0;
    lines.ri = (status & TIOCM_RI) != 0;
    lines.rts = (status & TIOCM_RTS) != 0;
    lines.dtr = (status & TIOCM_DTR) != 0;
    return lines;
}

bool PtyProxy::setRts(bool on) const {
    if (m_uartFd < 0) {
        return false;
    }
    int flag = TIOCM_RTS;
    return ::ioctl(m_uartFd, on ? TIOCMBIS : TIOCMBIC, &flag) == 0;
}

bool PtyProxy::setDtr(bool on) const {
    if (m_uartFd < 0) {
        return false;
    }
    int flag = TIOCM_DTR;
    return ::ioctl(m_uartFd, on ? TIOCMBIS : TIOCMBIC, &flag) == 0;
}

bool PtyProxy::sendBreak() const {
    if (m_uartFd < 0) {
        return false;
    }
    return ::tcsendbreak(m_uartFd, 0) == 0;
}

}  // namespace aether
