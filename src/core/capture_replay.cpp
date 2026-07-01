#include "core/capture_replay.h"

#include <QFile>
#include <QTimer>

#include <algorithm>

namespace aether {

namespace {

// pcap layout constants (mirrors PtyProxy's writer).
constexpr int kGlobalHeaderLen = 24;  ///< pcap file header.
constexpr int kRecordHeaderLen = 16;  ///< per-record header (ts_sec/ts_usec/incl_len/orig_len).
constexpr int kRtacHeaderLen = 12;    ///< RTAC serial pseudo-header preceding each payload.
constexpr int kRtacEventOffset = 8;   ///< event byte within the pseudo-header.
constexpr quint32 kPcapMagic = 0xa1b2c3d4U;
constexpr quint32 kLinkTypeRtacSerial = 250;
constexpr char kRtacEventTxStart = 0x01;  ///< DATA_TX_START (host -> peripheral).

/// Read a little-endian 32-bit word at @p off (caller guarantees 4 bytes exist).
quint32 readLe32(const QByteArray &buf, int off) {
    const auto b = [&](int i) { return static_cast<quint32>(static_cast<quint8>(buf.at(off + i))); };
    return b(0) | (b(1) << 8) | (b(2) << 16) | (b(3) << 24);
}

}  // namespace

std::optional<QVector<CapturedChunk>> readRtacPcap(const QString &path, QString *error) {
    const auto fail = [&](const QString &reason) -> std::optional<QVector<CapturedChunk>> {
        if (error != nullptr) {
            *error = reason;
        }
        return std::nullopt;
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("Cannot open %1: %2").arg(path, file.errorString()));
    }
    const QByteArray blob = file.readAll();
    if (blob.size() < kGlobalHeaderLen) {
        return fail(QStringLiteral("File is too small to be a pcap capture."));
    }
    if (readLe32(blob, 0) != kPcapMagic) {
        return fail(QStringLiteral("Not a little-endian pcap file (bad magic)."));
    }
    if (readLe32(blob, 20) != kLinkTypeRtacSerial) {
        return fail(QStringLiteral("Unsupported link type; expected RTAC_SERIAL (250)."));
    }

    QVector<CapturedChunk> chunks;
    int pos = kGlobalHeaderLen;
    while (pos + kRecordHeaderLen <= blob.size()) {
        const quint32 sec = readLe32(blob, pos);
        const quint32 usec = readLe32(blob, pos + 4);
        const quint32 inclLen = readLe32(blob, pos + 8);
        pos += kRecordHeaderLen;

        // A record must at least hold the RTAC pseudo-header and not overrun the file.
        if (inclLen < static_cast<quint32>(kRtacHeaderLen) || pos + static_cast<int>(inclLen) > blob.size()) {
            return fail(QStringLiteral("Truncated or corrupt record at byte offset %1.").arg(pos));
        }

        CapturedChunk chunk;
        chunk.timestampMs = static_cast<qint64>(sec) * 1000 + static_cast<qint64>(usec) / 1000;
        chunk.dir = blob.at(pos + kRtacEventOffset) == kRtacEventTxStart ? Direction::Tx : Direction::Rx;
        chunk.data = blob.mid(pos + kRtacHeaderLen, static_cast<int>(inclLen) - kRtacHeaderLen);
        chunks.append(chunk);

        pos += static_cast<int>(inclLen);
    }
    return chunks;
}

CaptureReplayer::CaptureReplayer(QObject *parent) : QObject(parent), m_timer(new QTimer(this)) {
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &CaptureReplayer::emitCurrent);
}

CaptureReplayer::~CaptureReplayer() = default;

bool CaptureReplayer::load(const QString &path, QString *error) {
    auto parsed = readRtacPcap(path, error);
    if (!parsed) {
        return false;
    }
    stop();
    m_chunks = std::move(*parsed);
    m_index = 0;
    return true;
}

void CaptureReplayer::start() {
    if (m_chunks.isEmpty()) {
        emit finished();
        return;
    }
    m_replaying = true;
    m_index = 0;
    // Emit the first chunk immediately; subsequent ones honour the recorded gaps.
    emitCurrent();
}

void CaptureReplayer::stop() {
    m_timer->stop();
    m_replaying = false;
    m_index = 0;
}

void CaptureReplayer::emitCurrent() {
    if (!m_replaying || m_index >= m_chunks.size()) {
        return;
    }
    emit chunkReplayed(m_chunks.at(m_index));
    ++m_index;
    scheduleNext();
}

void CaptureReplayer::scheduleNext() {
    if (m_index >= m_chunks.size()) {
        m_replaying = false;
        emit finished();
        return;
    }
    // Pace by the recorded inter-packet gap, clamped so idle stretches don't stall.
    const qint64 gap = m_chunks.at(m_index).timestampMs - m_chunks.at(m_index - 1).timestampMs;
    const int waitMs = static_cast<int>(std::clamp<qint64>(gap, 0, kMaxGapMs));
    m_timer->start(waitMs);
}

}  // namespace aether
