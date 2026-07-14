#include "core/common/pcap_writer.hpp"

#include <QFile>

namespace aether {

namespace {

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

PcapWriter::PcapWriter() = default;

PcapWriter::~PcapWriter() {
    close();
}

bool PcapWriter::open(const QString &path, QString *error) {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = file->errorString();
        }
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
    m_file = std::move(file);
    return true;
}

void PcapWriter::close() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_file.reset();
}

bool PcapWriter::isOpen() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_file != nullptr;
}

void PcapWriter::writePacket(qint64 timestampMs, Direction dir, const QByteArray &data) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_file) {
        return;
    }
    const auto sec = static_cast<quint32>(timestampMs / 1000);
    const auto usec = static_cast<quint32>((timestampMs % 1000) * 1000);
    const auto fill = static_cast<quint32>(kRtacHeaderLen + data.size());

    // resize(0) keeps the allocation, so steady-state packets reuse one
    // buffer and cost a single write().
    m_scratch.resize(0);
    m_scratch.reserve(16 + kRtacHeaderLen + data.size());
    appendLe32(m_scratch, sec);
    appendLe32(m_scratch, usec);
    appendLe32(m_scratch, fill);
    appendLe32(m_scratch, fill);
    appendBe32(m_scratch, sec);
    appendBe32(m_scratch, usec);
    m_scratch.append(static_cast<char>(dir == Direction::Tx ? 0x01 : 0x02));
    m_scratch.append(static_cast<char>(0x00));
    m_scratch.append(static_cast<char>(0x00));
    m_scratch.append(static_cast<char>(0x00));
    m_scratch.append(data);
    m_file->write(m_scratch);
}

}  // namespace aether
