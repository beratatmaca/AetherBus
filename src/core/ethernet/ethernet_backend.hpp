#pragma once

#include "core/ethernet/ethernet_types.hpp"
#include "core/common/i_bus_backend.hpp"

#include <QStringList>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <pcap.h>

#include <vector>

namespace aether {

/** @brief libpcap capture/injection backend implementing @ref aether::IBusBackend. */
class EthernetBackend : public IBusBackend {
    Q_OBJECT

public:
    explicit EthernetBackend(QObject *parent = nullptr);
    ~EthernetBackend() override;

    /** @brief Open pcap capture and injection on @p config.interfaceName. */
    bool open(const EthernetConfig &config);
    void close() final;
    [[nodiscard]] bool isRunning() const override;

    /** @brief Send a raw packet onto the network interface. */
    bool sendPacket(const QByteArray &rawFrame);

    /** @brief Consume all currently accumulated packet chunks from the thread-safe buffer. */
    std::vector<CapturedChunk> consumeBufferedChunks();

    /** @return names of network interfaces currently present (via pcap_findalldevs). */
    [[nodiscard]] static QStringList listInterfaces();

private:
    void runCaptureLoop();

    /**
     * @brief pcap_dispatch() callback (user data is the EthernetBackend*): builds a
     * CapturedChunk from one captured packet and pushes it onto the buffer.
     */
    static void packetCallback(u_char *user, const struct pcap_pkthdr *header, const u_char *pktData);

    /**
     * @brief Resolve @p ifaceName's own hardware address into @p outMac.
     * @return false (leaving @p outMac untouched) if it cannot be determined.
     */
    static bool resolveLocalMac(const QString &ifaceName, std::array<unsigned char, 6> &outMac);

    pcap_t *m_pcapHandle = nullptr;
    QString m_interfaceName;
    std::thread m_captureThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::mutex m_pcapMutex;

    std::array<unsigned char, 6> m_localMac{};
    bool m_hasLocalMac = false;

    std::mutex m_bufferMutex;
    std::vector<CapturedChunk> m_packetBuffer;

    std::deque<QByteArray> m_recentlySent;
    static constexpr std::size_t kMaxRecentlySent = 32;
};

}  // namespace aether
