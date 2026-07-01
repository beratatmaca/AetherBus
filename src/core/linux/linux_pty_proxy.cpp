#include "core/linux/linux_pty_proxy.h"
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

constexpr quint32 kLinkTypeRtacSerial = 250;
constexpr int kRtacHeaderLen = 12;

}  // namespace

LinuxPtyProxy::LinuxPtyProxy(PtyProxy *q) : PtyProxyImpl(q) {}

LinuxPtyProxy::~LinuxPtyProxy() {
    close();
}

bool LinuxPtyProxy::open(const SerialConfig &config) {
    if (const QString problem = config.validate(); !problem.isEmpty()) {
        emit q_ptr->errorOccurred(q_ptr->tr("Invalid serial configuration: %1").arg(problem));
        return false;
    }

    if (m_worker.joinable()) {
        close();
    }

    m_rxBytes.store(0);
    m_txBytes.store(0);
    m_droppedBytes.store(0);
    m_ioFailed.store(false);

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

    m_uartFd = ::open(config.device.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_uartFd < 0) {
        emit q_ptr->errorOccurred(q_ptr->tr("Cannot open %1: %2").arg(config.device, QString::fromLocal8Bit(strerror(errno))));
        return false;
    }
    if (!configureTermios(config)) {
        teardownDescriptors();
        return false;
    }

    m_directMode.store(config.directMode);
    if (m_directMode.load()) {
        m_slavePath = QStringLiteral("Direct Mode");
        m_masterFd = -1;
    } else {
        m_masterFd = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (m_masterFd < 0 || ::grantpt(m_masterFd) != 0 || ::unlockpt(m_masterFd) != 0) {
            emit q_ptr->errorOccurred(q_ptr->tr("Failed to allocate pseudo-terminal: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            teardownDescriptors();
            return false;
        }
        const char *slave = ::ptsname(m_masterFd);
        if (slave == nullptr) {
            emit q_ptr->errorOccurred(q_ptr->tr("ptsname() failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            teardownDescriptors();
            return false;
        }
        m_slavePath = QString::fromLocal8Bit(slave);

        const int flags = ::fcntl(m_masterFd, F_GETFL, 0);
        ::fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);

        int packetMode = 1;
        ::ioctl(m_masterFd, TIOCPKT, &packetMode);

        if (!config.symlinkPath.isEmpty()) {
            ::unlink(config.symlinkPath.toLocal8Bit().constData());
            if (::symlink(slave, config.symlinkPath.toLocal8Bit().constData()) == 0) {
                m_symlinkPath = config.symlinkPath;
            } else {
                emit q_ptr->errorOccurred(q_ptr->tr("Could not create symlink %1: %2").arg(config.symlinkPath, QString::fromLocal8Bit(strerror(errno))));
            }
        }
    }

    std::array<int, 2> pipeFds{-1, -1};
    if (::pipe(pipeFds.data()) != 0) {
        emit q_ptr->errorOccurred(q_ptr->tr("pipe() failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        teardownDescriptors();
        return false;
    }
    m_wakeReadFd = pipeFds[0];
    m_wakeWriteFd = pipeFds[1];
    ::fcntl(m_wakeReadFd, F_SETFL, O_NONBLOCK);

    m_cleanupSlot = registerCleanup(m_symlinkPath.isEmpty() ? nullptr : m_symlinkPath.toLocal8Bit().constData(), m_uartFd, m_masterFd);

    m_stopRequested.store(false);
    m_running.store(true);
    m_worker = std::thread(&LinuxPtyProxy::runLoop, this);

    emit q_ptr->started(m_slavePath);
    return true;
}

bool LinuxPtyProxy::configureTermios(const SerialConfig &config) {
    speed_t speed = B115200;
    const bool standardBaud = baudToSpeed(config.baud, speed);

    termios tio{};
    if (::tcgetattr(m_uartFd, &tio) != 0) {
        emit q_ptr->errorOccurred(q_ptr->tr("tcgetattr failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
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

    tio.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
    tio.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY);
    if (config.flow == FlowControl::RtsCts) {
        tio.c_cflag |= CRTSCTS;
    } else if (config.flow == FlowControl::XonXoff) {
        tio.c_iflag |= (IXON | IXOFF);
    }

    if (::tcsetattr(m_uartFd, TCSANOW, &tio) != 0) {
        emit q_ptr->errorOccurred(q_ptr->tr("tcsetattr failed: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return false;
    }

    if (!standardBaud) {
        if (!setCustomBaud(m_uartFd, config.baud)) {
            emit q_ptr->errorOccurred(q_ptr->tr("Unsupported baud rate: %1").arg(config.baud));
            return false;
        }
    }
    return true;
}

void LinuxPtyProxy::runLoop() {
    std::vector<char> buf(4096);
    bool deviceLost = false;

    while (!m_stopRequested.load()) {
        if (m_ioFailed.load()) {
            deviceLost = true;
            break;
        }

        const bool direct = m_directMode.load();

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

        if ((fds[wakeIdx].revents & POLLIN) != 0) {
            while (::read(m_wakeReadFd, buf.data(), buf.size()) > 0) {
            }
            continue;
        }

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
                emit q_ptr->chunkCaptured({ts, Direction::Rx, data});
                writePcapPacket(ts, Direction::Rx, data);
                if (!direct && m_masterFd >= 0) {
                    forward(m_masterFd, m_masterOut, Direction::Rx, data);
                }
            } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                deviceLost = true;
                break;
            }
        }

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
                            emit q_ptr->chunkCaptured({ts, Direction::Tx, data});
                            writePcapPacket(ts, Direction::Tx, data);
                            forward(m_uartFd, m_uartOut, Direction::Tx, data);
                        }
                    } else {
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
        emit q_ptr->disconnected();
    }
}

bool LinuxPtyProxy::drainLocked(int fd, OutQueue &queue) {
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
                return true;
            }
            return false;
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

bool LinuxPtyProxy::forward(int fd, OutQueue &queue, Direction dir, const QByteArray &data) {
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
        emit q_ptr->writeStalled(dir, droppedTotal);
    }
    if (fatal) {
        m_ioFailed.store(true);
        wakeLoop();
    }
    return !dropped && !fatal;
}

void LinuxPtyProxy::mirrorSlaveTermios() {
    if (m_uartFd < 0 || m_masterFd < 0) {
        return;
    }
    termios slaveTio{};
    if (::tcgetattr(m_masterFd, &slaveTio) != 0) {
        return;
    }
    termios uartTio{};
    if (::tcgetattr(m_uartFd, &uartTio) != 0) {
        return;
    }

    ::cfsetispeed(&uartTio, ::cfgetispeed(&slaveTio));
    ::cfsetospeed(&uartTio, ::cfgetospeed(&slaveTio));
    const tcflag_t cmask = CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS;
    uartTio.c_cflag = (uartTio.c_cflag & ~cmask) | (slaveTio.c_cflag & cmask);
    const tcflag_t imask = IXON | IXOFF | IXANY;
    uartTio.c_iflag = (uartTio.c_iflag & ~imask) | (slaveTio.c_iflag & imask);

    if (::tcsetattr(m_uartFd, TCSANOW, &uartTio) != 0) {
        emit q_ptr->errorOccurred(q_ptr->tr("Failed to mirror line settings to the device: %1").arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

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

    if (baud == m_lastBaud && dataBits == m_lastDataBits && parity == m_lastParity && stopBits == m_lastStopBits) {
        return;
    }
    m_lastBaud = baud;
    m_lastDataBits = dataBits;
    m_lastParity = parity;
    m_lastStopBits = stopBits;
    emit q_ptr->lineReconfigured(baud, dataBits, parity, stopBits);
}

void LinuxPtyProxy::wakeLoop() const {
    if (m_wakeWriteFd >= 0) {
        const char byte = 1;
        ssize_t rc = ::write(m_wakeWriteFd, &byte, 1);
        (void)rc;
    }
}

void LinuxPtyProxy::close() {
    if (m_worker.joinable()) {
        m_stopRequested.store(true);
        wakeLoop();
        m_worker.join();
    }
    m_running.store(false);
    teardownDescriptors();
    emit q_ptr->stopped();
}

void LinuxPtyProxy::teardownDescriptors() {
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

bool LinuxPtyProxy::injectToDevice(const QByteArray &bytes) {
    if (m_uartFd < 0 || bytes.isEmpty()) {
        return false;
    }
    const bool ok = forward(m_uartFd, m_uartOut, Direction::Tx, bytes);
    if (ok) {
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        emit q_ptr->chunkCaptured({ts, Direction::Tx, bytes});
        writePcapPacket(ts, Direction::Tx, bytes);
    }
    wakeLoop();
    return ok;
}

bool LinuxPtyProxy::injectToApp(const QByteArray &bytes) {
    if (m_masterFd < 0 || bytes.isEmpty()) {
        return false;
    }
    const bool ok = forward(m_masterFd, m_masterOut, Direction::Rx, bytes);
    if (ok) {
        const qint64 ts = QDateTime::currentMSecsSinceEpoch();
        emit q_ptr->chunkCaptured({ts, Direction::Rx, bytes});
        writePcapPacket(ts, Direction::Rx, bytes);
    }
    wakeLoop();
    return ok;
}

bool LinuxPtyProxy::isRunning() const {
    return m_running.load();
}

QString LinuxPtyProxy::slavePath() const {
    return m_slavePath;
}

PtyProxy::Stats LinuxPtyProxy::stats() const {
    PtyProxy::Stats s;
    s.rx = m_rxBytes.load();
    s.tx = m_txBytes.load();
    s.dropped = m_droppedBytes.load();
    return s;
}

bool LinuxPtyProxy::startCapture(const QString &path) {
    std::lock_guard<std::mutex> lk(m_captureMutex);
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit q_ptr->errorOccurred(q_ptr->tr("Cannot open capture file %1: %2").arg(path, file->errorString()));
        return false;
    }
    QByteArray header;
    appendLe32(header, 0xa1b2c3d4U);
    appendLe16(header, 2);
    appendLe16(header, 4);
    appendLe32(header, 0);
    appendLe32(header, 0);
    appendLe32(header, 262144);
    appendLe32(header, kLinkTypeRtacSerial);
    file->write(header);
    m_captureFile = std::move(file);
    return true;
}

void LinuxPtyProxy::stopCapture() {
    std::lock_guard<std::mutex> lk(m_captureMutex);
    m_captureFile.reset();
}

bool LinuxPtyProxy::isCapturing() const {
    std::lock_guard<std::mutex> lk(m_captureMutex);
    return m_captureFile != nullptr;
}

void LinuxPtyProxy::writePcapPacket(qint64 timestampMs, Direction dir, const QByteArray &data) {
    std::lock_guard<std::mutex> lk(m_captureMutex);
    if (!m_captureFile) {
        return;
    }
    const auto sec = static_cast<quint32>(timestampMs / 1000);
    const auto usec = static_cast<quint32>((timestampMs % 1000) * 1000);
    const auto fill = static_cast<quint32>(kRtacHeaderLen + data.size());

    QByteArray record;
    appendLe32(record, sec);
    appendLe32(record, usec);
    appendLe32(record, fill);
    appendLe32(record, fill);
    appendBe32(record, sec);
    appendBe32(record, usec);
    record.append(static_cast<char>(dir == Direction::Tx ? 0x01 : 0x02));
    record.append(static_cast<char>(0x00));
    record.append(static_cast<char>(0x00));
    record.append(static_cast<char>(0x00));
    record.append(data);
    m_captureFile->write(record);
}

PtyProxy::ModemLines LinuxPtyProxy::modemLines() const {
    PtyProxy::ModemLines lines;
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

bool LinuxPtyProxy::setRts(bool on) {
    if (m_uartFd < 0) {
        return false;
    }
    int flag = TIOCM_RTS;
    return ::ioctl(m_uartFd, on ? TIOCMBIS : TIOCMBIC, &flag) == 0;
}

bool LinuxPtyProxy::setDtr(bool on) {
    if (m_uartFd < 0) {
        return false;
    }
    int flag = TIOCM_DTR;
    return ::ioctl(m_uartFd, on ? TIOCMBIS : TIOCMBIC, &flag) == 0;
}

bool LinuxPtyProxy::sendBreak() {
    if (m_uartFd < 0) {
        return false;
    }
    return ::tcsendbreak(m_uartFd, 0) == 0;
}

}  // namespace aether
