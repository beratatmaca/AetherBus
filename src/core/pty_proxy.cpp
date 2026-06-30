#include "core/pty_proxy.h"

#include "core/linux_baud.h"

#include <QDateTime>
#include <QFile>

#include <array>
#include <cerrno>
#include <cstring>
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

// Best-effort full write that retries on partial writes and EINTR.
bool writeAll(int fd, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        const ssize_t n = ::write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;  // device buffer full; spin briefly
            }
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace

PtyProxy::PtyProxy(QObject *parent) : QObject(parent) {
    qRegisterMetaType<aether::CapturedChunk>("aether::CapturedChunk");
}

PtyProxy::~PtyProxy() {
    close();
}

bool PtyProxy::open(const SerialConfig &config) {
    if (m_running.load()) {
        close();
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

    if (config.parity == 'N') {
        tio.c_cflag &= ~static_cast<tcflag_t>(PARENB);
    } else {
        tio.c_cflag |= PARENB;
        if (config.parity == 'O') {
            tio.c_cflag |= PARODD;
        } else {
            tio.c_cflag &= ~static_cast<tcflag_t>(PARODD);
        }
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
        std::array<pollfd, 3> fds{};
        fds[0] = {m_uartFd, POLLIN, 0};
        fds[1] = {m_masterFd, POLLIN, 0};
        fds[2] = {m_wakeReadFd, POLLIN, 0};

        int nfds = 3;
        if (m_directMode.load()) {
            fds[1] = {m_wakeReadFd, POLLIN, 0};
            nfds = 2;
        }

        const int ready = ::poll(fds.data(), nfds, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            deviceLost = true;
            break;
        }

        // Wake pipe: drain and re-check the stop flag.
        bool wakeTriggered = m_directMode.load() ? ((fds[1].revents & POLLIN) != 0) : ((fds[2].revents & POLLIN) != 0);
        if (wakeTriggered) {
            while (::read(m_wakeReadFd, buf.data(), buf.size()) > 0) {
            }
            continue;
        }

        // Descriptor A: physical UART -> tag Rx, forward to PTY master. A read of
        // 0 or a hard error here means the device went away (e.g. unplugged).
        if ((fds[0].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0) {
            const ssize_t n = ::read(m_uartFd, buf.data(), buf.size());
            if (n > 0) {
                QByteArray data(buf.data(), static_cast<int>(n));
                CapturedChunk chunk{QDateTime::currentMSecsSinceEpoch(), Direction::Rx, data};
                emit chunkCaptured(chunk);
                if (!m_directMode.load() && m_masterFd >= 0) {
                    writeAll(m_masterFd, data.constData(), static_cast<size_t>(data.size()));
                }
            } else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                deviceLost = true;
                break;
            }
        }

        // Descriptor B: PTY master -> tag Tx, forward to physical UART (non-direct only).
        if (!m_directMode.load() && m_masterFd >= 0 && (fds[1].revents & (POLLIN | POLLHUP)) != 0) {
            const ssize_t n = ::read(m_masterFd, buf.data(), buf.size());
            if (n > 0) {
                QByteArray data(buf.data(), static_cast<int>(n));
                CapturedChunk chunk{QDateTime::currentMSecsSinceEpoch(), Direction::Tx, data};
                emit chunkCaptured(chunk);
                writeAll(m_uartFd, data.constData(), static_cast<size_t>(data.size()));
            }
        }
    }

    m_running.store(false);
    if (deviceLost && !m_stopRequested.load()) {
        emit disconnected();
    }
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
    const bool ok = writeAll(m_uartFd, bytes.constData(), static_cast<size_t>(bytes.size()));
    if (ok) {
        emit chunkCaptured({QDateTime::currentMSecsSinceEpoch(), Direction::Tx, bytes});
    }
    return ok;
}

bool PtyProxy::injectToApp(const QByteArray &bytes) {
    if (m_masterFd < 0 || bytes.isEmpty()) {
        return false;
    }
    const bool ok = writeAll(m_masterFd, bytes.constData(), static_cast<size_t>(bytes.size()));
    if (ok) {
        emit chunkCaptured({QDateTime::currentMSecsSinceEpoch(), Direction::Rx, bytes});
    }
    return ok;
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
