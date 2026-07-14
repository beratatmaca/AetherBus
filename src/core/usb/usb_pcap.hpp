#pragma once
#include <QByteArray>
#include <QFile>
#include <memory>

namespace aether {

/**
 * @brief Writer for raw USB PCAP files (compatible with Wireshark).
 */
class UsbPcapWriter {
public:
    UsbPcapWriter();
    ~UsbPcapWriter();

    UsbPcapWriter(const UsbPcapWriter &) = delete;
    UsbPcapWriter &operator=(const UsbPcapWriter &) = delete;

    /** @brief Open file at @p path with specified PCAP @p linkType. */
    bool open(const QString &path, uint32_t linkType, QString *error);
    void close();
    [[nodiscard]] bool isOpen() const;

    /** @brief Write raw packet payload to PCAP capture. */
    void writePacket(qint64 timestampMs, const QByteArray &data);

private:
    std::unique_ptr<QFile> m_file;
    QByteArray m_scratch;
};

}  // namespace aether
