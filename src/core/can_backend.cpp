#include "core/can_backend.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>

#if defined(__linux__)
#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <vector>
#endif

namespace aether {

namespace {
constexpr int kArphrdCan = 280;  ///< ARPHRD_CAN link type in /sys/class/net/<if>/type.

#if defined(__linux__)
QString errnoText() {
    return QString::fromLocal8Bit(std::strerror(errno));
}
#endif
}  // namespace

CanBackend::CanBackend(QObject *parent) : IBusBackend(parent) {
    qRegisterMetaType<aether::CapturedChunk>("aether::CapturedChunk");
    qRegisterMetaType<aether::Direction>("aether::Direction");
}

CanBackend::~CanBackend() {
    // Silent teardown (no stopped() emission during destruction).
    m_stopRequested.store(true);
    wakeLoop();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    teardown();
}

bool CanBackend::isSupported() {
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

QStringList CanBackend::listInterfaces() {
    QStringList result;
    QDir netDir(QStringLiteral("/sys/class/net"));
    if (!netDir.exists()) {
        return result;
    }
    const QStringList entries = netDir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::System);
    for (const QString &name : entries) {
        QFile typeFile(netDir.absoluteFilePath(name) + QStringLiteral("/type"));
        if (!typeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        bool ok = false;
        const int type = typeFile.readAll().trimmed().toInt(&ok);
        if (ok && type == kArphrdCan) {
            result << name;
        }
    }
    result.sort();
    return result;
}

int CanBackend::queryBitrate(const QString & /*iface*/) {
    // Deferred to a later phase: reading the bit rate requires an RTNL/netlink
    // query. Until then the connection info line omits it. Returns -1 = unknown.
    return -1;
}

bool CanBackend::isRunning() const {
    return m_running.load();
}

QString CanBackend::iface() const {
    return m_iface;
}

CanBackend::Stats CanBackend::stats() const {
    return {m_rxFrames.load(), m_txFrames.load(), m_dropped.load()};
}

void CanBackend::wakeLoop() const {
#if defined(__linux__)
    if (m_wakeWriteFd >= 0) {
        const char b = 1;
        const ssize_t rc = ::write(m_wakeWriteFd, &b, 1);
        (void)rc;  // best-effort; the pipe is drained in the loop
    }
#endif
}

void CanBackend::teardown() {
#if defined(__linux__)
    if (m_sockFd >= 0) {
        ::close(m_sockFd);
        m_sockFd = -1;
    }
    if (m_wakeReadFd >= 0) {
        ::close(m_wakeReadFd);
        m_wakeReadFd = -1;
    }
    if (m_wakeWriteFd >= 0) {
        ::close(m_wakeWriteFd);
        m_wakeWriteFd = -1;
    }
#endif
    m_iface.clear();
    m_fdMode = false;
    std::lock_guard<std::mutex> lk(m_txMutex);
    m_txQueue.clear();
}

void CanBackend::close() {
    const bool wasActive = m_worker.joinable();
    m_stopRequested.store(true);
    wakeLoop();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    teardown();
    m_running.store(false);
    m_stopRequested.store(false);
    if (wasActive) {
        emit stopped();
    }
}

#if defined(__linux__)

bool CanBackend::open(const CanConfig &config) {
    if (const QString problem = config.validate(); !problem.isEmpty()) {
        emit errorOccurred(tr("Invalid CAN configuration: %1").arg(problem));
        return false;
    }
    if (m_worker.joinable()) {
        close();
    }

    m_rxFrames.store(0);
    m_txFrames.store(0);
    m_dropped.store(0);
    m_ioFailed.store(false);
    m_stopRequested.store(false);

    const int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        emit errorOccurred(tr("socket(PF_CAN) failed: %1").arg(errnoText()));
        return false;
    }

    struct ifreq ifr {};
    const QByteArray ifaceBytes = config.iface.toLocal8Bit();
    std::strncpy(ifr.ifr_name, ifaceBytes.constData(), IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        emit errorOccurred(tr("CAN interface '%1' not found: %2").arg(config.iface, errnoText()));
        ::close(fd);
        return false;
    }

    // CAN-FD support is best-effort: if the interface/kernel does not allow it we
    // silently fall back to classic frames rather than failing the connection.
    m_fdMode = false;
    if (config.fdMode) {
        int on = 1;
        if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof(on)) == 0) {
            m_fdMode = true;
        }
    }

    {
        int on = config.loopback ? 1 : 0;
        ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &on, sizeof(on));
    }
    {
        int on = config.receiveOwn ? 1 : 0;
        ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &on, sizeof(on));
    }
    {
        can_err_mask_t mask = config.errorFrames ? CAN_ERR_MASK : 0;
        ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &mask, sizeof(mask));
    }

    if (!config.filters.isEmpty()) {
        std::vector<struct can_filter> filters;
        filters.reserve(static_cast<std::size_t>(config.filters.size()));
        for (const CanFilter &f : config.filters) {
            struct can_filter cf {};
            cf.can_id = f.id & (f.extended ? CAN_EFF_MASK : CAN_SFF_MASK);
            if (f.extended) {
                cf.can_id |= CAN_EFF_FLAG;
            }
            if (f.invert) {
                cf.can_id |= CAN_INV_FILTER;
            }
            cf.can_mask = f.mask | CAN_EFF_FLAG;  // distinguish SFF vs EFF ids
            filters.push_back(cf);
        }
        if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, filters.data(),
                         static_cast<socklen_t>(filters.size() * sizeof(struct can_filter))) < 0) {
            emit errorOccurred(tr("Failed to apply CAN filters: %1").arg(errnoText()));
            ::close(fd);
            return false;
        }
    }

    if (const int fl = ::fcntl(fd, F_GETFL, 0); fl >= 0) {
        ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }

    struct sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        emit errorOccurred(tr("bind to '%1' failed: %2").arg(config.iface, errnoText()));
        ::close(fd);
        return false;
    }

    std::array<int, 2> wake{-1, -1};
    if (::pipe(wake.data()) != 0) {
        emit errorOccurred(tr("Failed to create wake pipe: %1").arg(errnoText()));
        ::close(fd);
        return false;
    }
    for (const int wfd : wake) {
        if (const int fl = ::fcntl(wfd, F_GETFL, 0); fl >= 0) {
            ::fcntl(wfd, F_SETFL, fl | O_NONBLOCK);
        }
    }

    m_sockFd = fd;
    m_wakeReadFd = wake[0];
    m_wakeWriteFd = wake[1];
    m_iface = config.iface;
    m_running.store(true);
    m_worker = std::thread([this] { runLoop(); });

    const int bitrate = queryBitrate(config.iface);
    const QString fdSuffix = m_fdMode ? tr(" (FD)") : QString();
    const QString info =
        bitrate > 0 ? tr("%1 @ %2 bit/s%3").arg(config.iface).arg(bitrate).arg(fdSuffix) : tr("%1%2").arg(config.iface, fdSuffix);
    emit started(info);
    return true;
}

bool CanBackend::sendFrame(quint32 id, quint16 flags, const QByteArray &payload) {
    if (!m_running.load() || m_sockFd < 0) {
        return false;
    }

    const bool extended = (flags & FrameExtendedId) != 0;
    const bool remote = (flags & FrameRemote) != 0;
    const bool wantFd = m_fdMode && ((flags & FrameFd) != 0 || payload.size() > 8);

    canid_t canId = extended ? ((id & CAN_EFF_MASK) | CAN_EFF_FLAG) : (id & CAN_SFF_MASK);
    if (remote) {
        canId |= CAN_RTR_FLAG;
    }

    QByteArray blob;
    if (wantFd) {
        if (payload.size() > 64) {
            return false;
        }
        struct canfd_frame f {};
        f.can_id = canId;
        f.len = static_cast<__u8>(payload.size());
        if ((flags & FrameBitRateSwitch) != 0) {
            f.flags |= CANFD_BRS;
        }
        if ((flags & FrameErrorStateInd) != 0) {
            f.flags |= CANFD_ESI;
        }
        std::memcpy(f.data, payload.constData(), static_cast<std::size_t>(payload.size()));
        blob = QByteArray(reinterpret_cast<const char *>(&f), static_cast<int>(CANFD_MTU));
    } else {
        if (payload.size() > 8) {
            return false;
        }
        struct can_frame f {};
        f.can_id = canId;
        f.can_dlc = static_cast<__u8>(remote ? 0 : payload.size());
        if (!remote) {
            std::memcpy(f.data, payload.constData(), static_cast<std::size_t>(payload.size()));
        }
        blob = QByteArray(reinterpret_cast<const char *>(&f), static_cast<int>(CAN_MTU));
    }

    {
        std::lock_guard<std::mutex> lk(m_txMutex);
        m_txQueue.push_back(blob);
    }
    wakeLoop();

    // Reflect the transmit in the capture stream immediately, independent of the
    // socket loopback setting, so the console always shows what we sent.
    CapturedChunk chunk;
    chunk.timestampMs = QDateTime::currentMSecsSinceEpoch();
    chunk.dir = Direction::Tx;
    chunk.isFrame = true;
    chunk.frameId = id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    chunk.frameFlags = flags;
    if (!remote) {
        chunk.data = payload;
    }
    m_txFrames.fetch_add(1);
    emit chunkCaptured(chunk);
    return true;
}

void CanBackend::runLoop() {
    bool deviceLost = false;
    struct canfd_frame frame {};
    std::array<char, 256> drain{};

    while (!m_stopRequested.load()) {
        short sockEvents = POLLIN;
        {
            std::lock_guard<std::mutex> lk(m_txMutex);
            if (!m_txQueue.empty()) {
                sockEvents |= POLLOUT;
            }
        }

        std::array<pollfd, 2> fds{};
        fds[0] = {m_sockFd, sockEvents, 0};
        fds[1] = {m_wakeReadFd, POLLIN, 0};

        const int ready = ::poll(fds.data(), fds.size(), -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            deviceLost = true;
            break;
        }

        if ((fds[1].revents & POLLIN) != 0) {
            while (::read(m_wakeReadFd, drain.data(), drain.size()) > 0) {
            }
        }

        const short rev = fds[0].revents;

        if ((rev & POLLOUT) != 0) {
            std::lock_guard<std::mutex> lk(m_txMutex);
            while (!m_txQueue.empty()) {
                const QByteArray &blob = m_txQueue.front();
                const ssize_t n = ::write(m_sockFd, blob.constData(), static_cast<std::size_t>(blob.size()));
                if (n == blob.size()) {
                    m_txQueue.pop_front();
                } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    break;  // socket buffer full; retry on the next POLLOUT
                } else {
                    m_dropped.fetch_add(1);
                    m_txQueue.pop_front();  // discard an un-writable frame
                }
            }
        }

        if ((rev & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0) {
            for (;;) {
                const ssize_t n = ::recv(m_sockFd, &frame, sizeof(frame), 0);
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                        break;
                    }
                    deviceLost = true;
                    break;
                }
                if (n == 0) {
                    break;
                }

                const bool isFd = (n == static_cast<ssize_t>(CANFD_MTU));
                const canid_t rawId = frame.can_id;

                CapturedChunk chunk;
                chunk.timestampMs = QDateTime::currentMSecsSinceEpoch();
                chunk.dir = Direction::Rx;
                chunk.isFrame = true;

                quint16 flags = 0;
                if ((rawId & CAN_EFF_FLAG) != 0) {
                    flags |= FrameExtendedId;
                    chunk.frameId = rawId & CAN_EFF_MASK;
                } else {
                    chunk.frameId = rawId & CAN_SFF_MASK;
                }
                if ((rawId & CAN_RTR_FLAG) != 0) {
                    flags |= FrameRemote;
                }
                if ((rawId & CAN_ERR_FLAG) != 0) {
                    flags |= FrameError;
                }

                int len = frame.len;
                if (isFd) {
                    flags |= FrameFd;
                    if ((frame.flags & CANFD_BRS) != 0) {
                        flags |= FrameBitRateSwitch;
                    }
                    if ((frame.flags & CANFD_ESI) != 0) {
                        flags |= FrameErrorStateInd;
                    }
                    len = qBound(0, len, 64);
                } else {
                    len = qBound(0, len, 8);
                }
                chunk.frameFlags = flags;
                if ((flags & FrameRemote) == 0 && len > 0) {
                    chunk.data = QByteArray(reinterpret_cast<const char *>(frame.data), len);
                }

                m_rxFrames.fetch_add(1);
                emit chunkCaptured(chunk);
            }
            if (deviceLost) {
                break;
            }
        }
    }

    m_running.store(false);
    if (deviceLost && !m_stopRequested.load()) {
        emit disconnected();
    }
}

#else  // !defined(__linux__) — SocketCAN unavailable; safe no-op stubs.

bool CanBackend::open(const CanConfig & /*config*/) {
    emit errorOccurred(tr("CAN support is not available on this platform."));
    return false;
}

bool CanBackend::sendFrame(quint32 /*id*/, quint16 /*flags*/, const QByteArray & /*payload*/) {
    return false;
}

void CanBackend::runLoop() {}

#endif

}  // namespace aether
