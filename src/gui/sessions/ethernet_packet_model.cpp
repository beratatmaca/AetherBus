#include "gui/sessions/ethernet_packet_model.hpp"

#include <QColor>
#include <QDateTime>
#include <QHostAddress>
#include <QtEndian>

namespace aether {

namespace {

const QColor kRxColor(0x00, 0xbc, 0xd4);  // Cyan
const QColor kTxColor(0xff, 0x98, 0x00);  // Orange

QString formatMac(const uint8_t *bytes) {
    return QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(bytes[0], 2, 16, QChar('0'))
        .arg(bytes[1], 2, 16, QChar('0'))
        .arg(bytes[2], 2, 16, QChar('0'))
        .arg(bytes[3], 2, 16, QChar('0'))
        .arg(bytes[4], 2, 16, QChar('0'))
        .arg(bytes[5], 2, 16, QChar('0'))
        .toUpper();
}

}  // namespace

EthernetPacketModel::EthernetPacketModel(QObject *parent) : QAbstractTableModel(parent) {}

int EthernetPacketModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return packetCount();
}

int EthernetPacketModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColCount;
}

QVariant EthernetPacketModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= packetCount()) {
        return {};
    }
    const Row &row = m_rows[static_cast<std::size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case ColTime:
                return QDateTime::fromMSecsSinceEpoch(row.chunk.timestampMs).toString(QStringLiteral("hh:mm:ss.zzz"));
            case ColDir:
                return row.chunk.dir == Direction::Rx ? tr("Rx") : tr("Tx");
            case ColSource:
                return row.source;
            case ColDestination:
                return row.destination;
            case ColProtocol:
                return row.protocol;
            case ColLength:
                return row.chunk.data.size();
            default:
                return {};
        }
    }
    if (role == Qt::ForegroundRole) {
        return row.chunk.dir == Direction::Rx ? kRxColor : kTxColor;
    }
    return {};
}

QVariant EthernetPacketModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ColTime:
            return tr("Time");
        case ColDir:
            return tr("Dir");
        case ColSource:
            return tr("Source");
        case ColDestination:
            return tr("Destination");
        case ColProtocol:
            return tr("Protocol");
        case ColLength:
            return tr("Length");
        default:
            return {};
    }
}

void EthernetPacketModel::appendPacket(const CapturedChunk &chunk) {
    if (packetCount() >= kMaxRows) {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_rows.pop_front();
        endRemoveRows();
    }

    const int newRow = packetCount();
    beginInsertRows(QModelIndex(), newRow, newRow);
    m_rows.push_back(summarize(chunk));
    endInsertRows();
}

void EthernetPacketModel::clearPackets() {
    if (m_rows.empty()) {
        return;
    }
    beginRemoveRows(QModelIndex(), 0, packetCount() - 1);
    m_rows.clear();
    endRemoveRows();
}

const CapturedChunk &EthernetPacketModel::chunkAt(int row) const {
    return m_rows[static_cast<std::size_t>(row)].chunk;
}

EthernetPacketModel::Row EthernetPacketModel::summarize(const CapturedChunk &chunk) {
    Row row;
    row.chunk = chunk;
    row.protocol = tr("Raw");
    row.source = tr("Unknown");
    row.destination = tr("Unknown");

    const QByteArray &data = chunk.data;
    if (data.size() >= 14) {
        // Ethernet Layer
        const auto *mac = reinterpret_cast<const uint8_t *>(data.constData());
        row.destination = formatMac(mac);
        row.source = formatMac(mac + 6);

        const uint16_t etherType = (mac[12] << 8) | mac[13];
        if (etherType == 0x0800 && data.size() >= 34) {
            // IPv4 Layer
            const uint8_t *ip = mac + 14;
            row.source = QHostAddress(qFromBigEndian<quint32>(ip + 12)).toString();
            row.destination = QHostAddress(qFromBigEndian<quint32>(ip + 16)).toString();

            const uint8_t ipProto = ip[9];
            if (ipProto == 17) {
                row.protocol = QStringLiteral("UDP");
            } else if (ipProto == 6) {
                row.protocol = QStringLiteral("TCP");
            } else if (ipProto == 1) {
                row.protocol = QStringLiteral("ICMP");
            } else {
                row.protocol = tr("IPv4 (IP Proto %1)").arg(ipProto);
            }
        }
    }
    return row;
}

}  // namespace aether
