#pragma once

#include "core/common/i_bus_backend.hpp"
#include "core/usb/usb_types.hpp"

#include <QStringList>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#ifdef AETHER_HAVE_PCAP
#include <pcap.h>
#endif

namespace aether {

/**
 * @brief libpcap capture backend for USB traffic, implementing @ref aether::IBusBackend.
 */
class UsbBackend : public IBusBackend {
    Q_OBJECT

public:
    explicit UsbBackend(QObject *parent = nullptr);
    ~UsbBackend() override;

    /** @brief Open pcap capture on @p config.interfaceName. */
    bool open(const UsbConfig &config);
    void close() final;
    [[nodiscard]] bool isRunning() const override;

    /** @brief Consume all currently accumulated packet chunks from the thread-safe buffer. */
    std::vector<CapturedChunk> consumeBufferedChunks();

    /** @return Names of USB capture interfaces currently present (usbmon or USBPcap). */
    [[nodiscard]] static QStringList listInterfaces();

    /** @return The pcap datalink type for the active interface. */
    [[nodiscard]] uint32_t linkType() const;

#ifdef AETHER_HAVE_LIBUSB
    struct UsbDeviceInfo {
        uint16_t vid = 0;
        uint16_t pid = 0;
        uint8_t busNum = 0;
        uint8_t devNum = 0;
        QString description;
    };
    [[nodiscard]] static QList<UsbDeviceInfo> listDevices();
    static bool injectControlTransfer(uint16_t vid, uint16_t pid, uint8_t bmRequestType, uint8_t bRequest,
                                      uint16_t wValue, uint16_t wIndex, const QByteArray &data, QString *error);
    static bool injectBulkTransfer(uint16_t vid, uint16_t pid, uint8_t endpoint, const QByteArray &data, QString *error);
#endif

private:
    void runCaptureLoop();

    /**
     * @brief pcap_dispatch() callback (user data is the UsbBackend*): builds a
     * CapturedChunk from one captured USB packet and pushes it onto the buffer.
     */
#ifdef AETHER_HAVE_PCAP
    static void packetCallback(u_char *user, const struct pcap_pkthdr *header, const u_char *pktData);
    pcap_t *m_pcapHandle = nullptr;
#else
    void *m_pcapHandle = nullptr;
#endif
    QString m_interfaceName;
    std::thread m_captureThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::mutex m_pcapMutex;

    std::mutex m_bufferMutex;
    std::vector<CapturedChunk> m_packetBuffer;
};

}  // namespace aether
