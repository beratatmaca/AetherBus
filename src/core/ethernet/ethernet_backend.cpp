#include "core/ethernet/ethernet_backend.hpp"
#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <cstring>

#if defined(__linux__)
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace aether {

namespace {
// libpcap reports permission failures as plain text in errbuf (no distinct
// errno path through pcap_open_live), typically "socket: Operation not
// permitted" or "You don't have permission to capture on that device" —
// match on that instead of the exact wording.
bool looksLikePermissionError(const QString &pcapError) {
    return pcapError.contains(QStringLiteral("ermission"), Qt::CaseInsensitive);
}

QString rawCaptureHint() {
    return EthernetBackend::tr(
        " Raw packet capture needs elevated privileges. Either run as root, or grant "
        "this binary the capability once: 'sudo setcap cap_net_raw,cap_net_admin=eip "
        "<path to aetherbus>' — no root needed after that.");
}
}  // namespace

EthernetBackend::EthernetBackend(QObject *parent) : IBusBackend(parent) {}

EthernetBackend::~EthernetBackend() {
    close();
}

bool EthernetBackend::open(const EthernetConfig &config) {
    close();

    std::array<char, PCAP_ERRBUF_SIZE> errbuf{};
    m_interfaceName = config.interfaceName;

    // Open interface in live capture mode
    m_pcapHandle = pcap_open_live(m_interfaceName.toLocal8Bit().constData(),
                                  65535,  // Snapshot length (capture full packets)
                                  config.promiscuous ? 1 : 0,
                                  100,  // Read timeout in ms
                                  errbuf.data());

    if (!m_pcapHandle) {
        const QString pcapError = QString::fromLocal8Bit(errbuf.data());
        QString message = tr("pcap_open_live failed: %1").arg(pcapError);
        if (looksLikePermissionError(pcapError)) {
            message += rawCaptureHint();
        }
        emit errorOccurred(message);
        return false;
    }

    // Compile and set BPF filter if specified
    if (!config.bpfFilter.isEmpty()) {
        struct bpf_program fp;
        if (pcap_compile(m_pcapHandle, &fp, config.bpfFilter.toLocal8Bit().constData(), 1, PCAP_NETMASK_UNKNOWN) < 0) {
            emit errorOccurred(
                tr("Failed to compile BPF filter '%1': %2").arg(config.bpfFilter, QString::fromLocal8Bit(pcap_geterr(m_pcapHandle))));
            pcap_close(m_pcapHandle);
            m_pcapHandle = nullptr;
            return false;
        }

        if (pcap_setfilter(m_pcapHandle, &fp) < 0) {
            emit errorOccurred(tr("Failed to set BPF filter: %1").arg(QString::fromLocal8Bit(pcap_geterr(m_pcapHandle))));
            pcap_freecode(&fp);
            pcap_close(m_pcapHandle);
            m_pcapHandle = nullptr;
            return false;
        }
        pcap_freecode(&fp);
    }

    m_hasLocalMac = resolveLocalMac(m_interfaceName, m_localMac);

    m_running = true;
    m_stopRequested = false;

    // Start background capture thread
    m_captureThread = std::thread(&EthernetBackend::runCaptureLoop, this);

    emit started(tr("Interface: %1").arg(m_interfaceName));
    return true;
}

void EthernetBackend::close() {
    m_stopRequested = true;
    if (m_pcapHandle) {
        pcap_breakloop(m_pcapHandle);
    }

    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }

    std::lock_guard<std::mutex> lock(m_pcapMutex);
    if (m_pcapHandle) {
        pcap_close(m_pcapHandle);
        m_pcapHandle = nullptr;
        m_running = false;
        emit stopped();
    }
}

bool EthernetBackend::isRunning() const {
    return m_running;
}

bool EthernetBackend::sendPacket(const QByteArray &rawFrame) {
    std::lock_guard<std::mutex> lock(m_pcapMutex);
    if (!m_pcapHandle) {
        return false;
    }

    if (pcap_sendpacket(m_pcapHandle, reinterpret_cast<const u_char *>(rawFrame.constData()), rawFrame.size()) < 0) {
        qWarning() << "Failed to inject packet:" << pcap_geterr(m_pcapHandle);
        return false;
    }

    // Echo injected packet as a Tx chunk to the GUI buffer
    CapturedChunk chunk;
    chunk.timestampMs = QDateTime::currentMSecsSinceEpoch();
    chunk.dir = Direction::Tx;
    chunk.data = rawFrame;
    chunk.isFrame = false;

    {
        std::lock_guard<std::mutex> bufferLock(m_bufferMutex);
        m_packetBuffer.push_back(chunk);

        m_recentlySent.push_back(rawFrame);
        if (m_recentlySent.size() > kMaxRecentlySent) {
            m_recentlySent.pop_front();
        }
    }

    return true;
}

std::vector<CapturedChunk> EthernetBackend::consumeBufferedChunks() {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    std::vector<CapturedChunk> temp;
    temp.swap(m_packetBuffer);
    // The swap left the buffer with zero capacity; pre-size it to roughly the
    // last drain so it doesn't regrow from scratch every 33 ms tick.
    if (!temp.empty()) {
        m_packetBuffer.reserve(std::min<std::size_t>(temp.size(), 4096));
    }
    return temp;
}

bool EthernetBackend::resolveLocalMac(const QString &ifaceName, std::array<unsigned char, 6> &outMac) {
#if defined(__linux__)
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    struct ifreq ifr {};
    strncpy(ifr.ifr_name, ifaceName.toLocal8Bit().constData(), IFNAMSIZ - 1);

    bool ok = ioctl(fd, SIOCGIFHWADDR, &ifr) == 0;
    if (ok) {
        std::memcpy(outMac.data(), ifr.ifr_hwaddr.sa_data, outMac.size());
    }
    ::close(fd);
    return ok;
#else
    Q_UNUSED(ifaceName);
    Q_UNUSED(outMac);
    return false;
#endif
}

QStringList EthernetBackend::listInterfaces() {
    QStringList names;
    pcap_if_t *alldevs;
    std::array<char, PCAP_ERRBUF_SIZE> errbuf;

    if (pcap_findalldevs(&alldevs, errbuf.data()) == 0) {
        pcap_if_t *d = alldevs;
        while (d) {
            names.append(QString::fromLocal8Bit(d->name));
            d = d->next;
        }
        pcap_freealldevs(alldevs);
    }
    return names;
}

void EthernetBackend::packetCallback(u_char *user, const struct pcap_pkthdr *header, const u_char *pktData) {
    auto *self = reinterpret_cast<EthernetBackend *>(user);

    CapturedChunk chunk;
    chunk.timestampMs = (static_cast<qint64>(header->ts.tv_sec) * 1000) + (header->ts.tv_usec / 1000);
    chunk.data = QByteArray(reinterpret_cast<const char *>(pktData), static_cast<int>(header->caplen));
    chunk.isFrame = false;
    chunk.dir = Direction::Rx;
    if (self->m_hasLocalMac && chunk.data.size() >= 12) {
        const auto *srcMac = reinterpret_cast<const unsigned char *>(chunk.data.constData() + 6);
        if (std::memcmp(srcMac, self->m_localMac.data(), self->m_localMac.size()) == 0) {
            chunk.dir = Direction::Tx;
        }
    }

    std::lock_guard<std::mutex> bufferLock(self->m_bufferMutex);
    if (chunk.dir == Direction::Tx) {
        auto it = std::find(self->m_recentlySent.begin(), self->m_recentlySent.end(), chunk.data);
        if (it != self->m_recentlySent.end()) {
            self->m_recentlySent.erase(it);
            return;  // our own send echoed back by the kernel; already logged as Tx
        }
    }
    self->m_packetBuffer.push_back(chunk);
}

void EthernetBackend::runCaptureLoop() {
    while (!m_stopRequested) {
        // Drain everything the kernel has buffered with one libpcap call under
        // one lock acquisition, instead of a lock + pcap_next_ex round-trip per
        // packet. Blocks up to the pcap_open_live() read timeout when idle,
        // exactly as pcap_next_ex did.
        int res;
        {
            std::lock_guard<std::mutex> pcapLock(m_pcapMutex);
            res = pcap_dispatch(m_pcapHandle, -1, &EthernetBackend::packetCallback, reinterpret_cast<u_char *>(this));
        }
        if (res < 0) {
            // PCAP_ERROR_BREAK is the pcap_breakloop() shutdown path; anything
            // else is a real capture error.
            if (res != PCAP_ERROR_BREAK && !m_stopRequested) {
                emit errorOccurred(QString::fromLocal8Bit(pcap_geterr(m_pcapHandle)));
                emit disconnected();
            }
            break;
        }
    }
    m_running = false;
}

}  // namespace aether
