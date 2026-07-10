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

EthernetBackend::EthernetBackend(QObject *parent) : IBusBackend(parent) {}

EthernetBackend::~EthernetBackend() {
    close();
}

bool EthernetBackend::open(const EthernetConfig &config) {
    close();

    std::array<char, PCAP_ERRBUF_SIZE> errbuf;
    m_interfaceName = config.interfaceName;

    // Open interface in live capture mode
    m_pcapHandle = pcap_open_live(m_interfaceName.toLocal8Bit().constData(),
                                  65535,  // Snapshot length (capture full packets)
                                  config.promiscuous ? 1 : 0,
                                  100,  // Read timeout in ms
                                  errbuf.data());

    if (!m_pcapHandle) {
        emit errorOccurred(tr("pcap_open_live failed: %1").arg(QString::fromLocal8Bit(errbuf)));
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

void EthernetBackend::runCaptureLoop() {
    while (!m_stopRequested) {
        struct pcap_pkthdr *header;
        const u_char *pkt_data;

        int res;
        {
            std::lock_guard<std::mutex> pcapLock(m_pcapMutex);
            res = pcap_next_ex(m_pcapHandle, &header, &pkt_data);
        }
        if (res > 0) {
            CapturedChunk chunk;
            chunk.timestampMs = (static_cast<qint64>(header->ts.tv_sec) * 1000) + (header->ts.tv_usec / 1000);
            chunk.data = QByteArray(reinterpret_cast<const char *>(pkt_data), static_cast<int>(header->caplen));
            chunk.isFrame = false;
            chunk.dir = Direction::Rx;
            if (m_hasLocalMac && chunk.data.size() >= 12) {
                const auto *srcMac = reinterpret_cast<const unsigned char *>(chunk.data.constData() + 6);
                if (std::memcmp(srcMac, m_localMac.data(), m_localMac.size()) == 0) {
                    chunk.dir = Direction::Tx;
                }
            }

            std::lock_guard<std::mutex> bufferLock(m_bufferMutex);
            if (chunk.dir == Direction::Tx) {
                auto it = std::find(m_recentlySent.begin(), m_recentlySent.end(), chunk.data);
                if (it != m_recentlySent.end()) {
                    m_recentlySent.erase(it);
                    continue;
                }
            }
            m_packetBuffer.push_back(chunk);
        } else if (res < 0) {
            // Error occurred or loop broken
            if (!m_stopRequested) {
                emit errorOccurred(QString::fromLocal8Bit(pcap_geterr(m_pcapHandle)));
                emit disconnected();
            }
            break;
        }
    }
    m_running = false;
}

}  // namespace aether
