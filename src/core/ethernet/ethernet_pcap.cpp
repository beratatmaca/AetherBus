#include "core/ethernet/ethernet_pcap.hpp"

#include <QDataStream>
#include <QFile>

namespace aether {

namespace {

constexpr int kPcapGlobalHeaderLen = 24;
constexpr int kPcapRecordHeaderLen = 16;
constexpr quint32 kPcapMagic = 0xa1b2c3d4U;
constexpr quint32 kPcapLinkTypeEthernet = 1;

quint32 readLe32(const QByteArray &blob, int off) {
    return static_cast<quint32>(static_cast<quint8>(blob[off])) | (static_cast<quint32>(static_cast<quint8>(blob[off + 1])) << 8) |
           (static_cast<quint32>(static_cast<quint8>(blob[off + 2])) << 16) |
           (static_cast<quint32>(static_cast<quint8>(blob[off + 3])) << 24);
}

}  // namespace

std::optional<QVector<CapturedChunk>> readEthernetPcap(const QString &path, QString *error) {
    const auto fail = [&](const QString &reason) -> std::optional<QVector<CapturedChunk>> {
        if (error) {
            *error = reason;
        }
        return std::nullopt;
    };

    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        return fail(QStringLiteral("Cannot open %1: %2").arg(path, file.errorString()));
    }
    const QByteArray blob = file.readAll();
    if (blob.size() < kPcapGlobalHeaderLen) {
        return fail(QStringLiteral("File is too small to be a pcap capture."));
    }
    if (readLe32(blob, 0) != kPcapMagic) {
        return fail(QStringLiteral("Not a little-endian classic pcap file (pcapng is not supported yet)."));
    }
    if (readLe32(blob, 20) != kPcapLinkTypeEthernet) {
        return fail(QStringLiteral("Unsupported link type; expected Ethernet (LINKTYPE_ETHERNET=1)."));
    }

    QVector<CapturedChunk> chunks;
    int pos = kPcapGlobalHeaderLen;
    while (pos + kPcapRecordHeaderLen <= blob.size()) {
        const quint32 sec = readLe32(blob, pos);
        const quint32 usec = readLe32(blob, pos + 4);
        const quint32 inclLen = readLe32(blob, pos + 8);
        pos += kPcapRecordHeaderLen;

        if (pos + static_cast<int>(inclLen) > blob.size()) {
            return fail(QStringLiteral("Truncated record at byte offset %1.").arg(pos));
        }

        CapturedChunk chunk;
        chunk.timestampMs = static_cast<qint64>(sec) * 1000 + static_cast<qint64>(usec) / 1000;
        chunk.data = blob.mid(pos, static_cast<int>(inclLen));
        chunk.dir = Direction::Tx;
        chunk.isFrame = false;
        chunks.append(chunk);

        pos += static_cast<int>(inclLen);
    }
    return chunks;
}

EthernetPcapWriter::EthernetPcapWriter() = default;

EthernetPcapWriter::~EthernetPcapWriter() {
    close();
}

bool EthernetPcapWriter::open(const QString &path, QString *error) {
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
    out << kPcapLinkTypeEthernet;

    m_file = std::move(file);
    return true;
}

void EthernetPcapWriter::close() {
    if (m_file) {
        m_file->close();
        m_file.reset();
    }
}

bool EthernetPcapWriter::isOpen() const {
    return m_file != nullptr;
}

void EthernetPcapWriter::writePacket(qint64 timestampMs, const QByteArray &data) {
    if (!m_file) {
        return;
    }

    const auto sec = static_cast<quint32>(timestampMs / 1000);
    const auto usec = static_cast<quint32>((timestampMs % 1000) * 1000);
    const auto len = static_cast<quint32>(data.size());

    // One reused buffer, one write() per packet — no per-packet QDataStream.
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
