#include "core/usb/usb_pcap.hpp"
#include <QDataStream>

namespace aether {

namespace {
constexpr quint32 kPcapMagic = 0xa1b2c3d4U;
}

UsbPcapWriter::UsbPcapWriter() = default;

UsbPcapWriter::~UsbPcapWriter() {
    close();
}

bool UsbPcapWriter::open(const QString &path, uint32_t linkType, QString *error) {
    close();

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QFile::WriteOnly)) {
        if (error) {
            *error = file->errorString();
        }
        return false;
    }

    QDataStream out(file.get());
    out.setByteOrder(QDataStream::LittleEndian);
    out << kPcapMagic;
    out << static_cast<quint16>(2);      // version major
    out << static_cast<quint16>(4);      // version minor
    out << static_cast<qint32>(0);       // GMT to local correction
    out << static_cast<quint32>(0);      // accuracy of timestamps
    out << static_cast<quint32>(65535);  // max length of captured packets
    out << static_cast<quint32>(linkType);

    m_file = std::move(file);
    return true;
}

void UsbPcapWriter::close() {
    if (m_file) {
        m_file->close();
        m_file.reset();
    }
}

bool UsbPcapWriter::isOpen() const {
    return m_file != nullptr;
}

void UsbPcapWriter::writePacket(qint64 timestampMs, const QByteArray &data) {
    if (!m_file) {
        return;
    }

    const auto sec = static_cast<quint32>(timestampMs / 1000);
    const auto usec = static_cast<quint32>((timestampMs % 1000) * 1000);
    const auto len = static_cast<quint32>(data.size());

    m_scratch.resize(0);
    m_scratch.reserve(16 + data.size());
    const auto appendLe32 = [this](quint32 v) {
        m_scratch.append(static_cast<char>(v & 0xFF));
        m_scratch.append(static_cast<char>((v >> 8) & 0xFF));
        m_scratch.append(static_cast<char>((v >> 16) & 0xFF));
        m_scratch.append(static_cast<char>((v >> 24) & 0xFF));
    };
    appendLe32(sec);
    appendLe32(usec);
    appendLe32(len);  // saved size
    appendLe32(len);  // original size
    m_scratch.append(data);
    m_file->write(m_scratch);
}

}  // namespace aether
