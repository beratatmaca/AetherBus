#include "frametablemodel.h"
#include <QDateTime>

FrameTableModel::FrameTableModel(QObject *parent)
    : QAbstractTableModel(parent), m_startTime(QDateTime::currentMSecsSinceEpoch()) {}

int FrameTableModel::rowCount(const QModelIndex &/*parent*/) const {
    return m_frames.count();
}

int FrameTableModel::columnCount(const QModelIndex &/*parent*/) const {
    return 7;
}

QVariant FrameTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_frames.count()) {
        return QVariant();
    }

    const AetherFrame& frame = m_frames.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: // Index
                return index.row();
            case 1: // Timestamp (Seconds since start)
                return QString::number(static_cast<double>(frame.timestamp_us) / 1000000.0, 'f', 6);
            case 2: { // Delta Time (us)
                if (index.row() == 0) {
                    return QString("0.000000");
                }
                uint64_t prevTs = m_frames.at(index.row() - 1).timestamp_us;
                return QString::number(static_cast<double>(frame.timestamp_us - prevTs) / 1000000.0, 'f', 6);
            }
            case 3: // ID
                return QString("0x%1").arg(frame.payload_id, 0, 16).toUpper();
            case 4: // Bus/Interface
                return QString("Bus %1").arg(frame.bus_identifier);
            case 5: // Length
                return frame.length;
            case 6: { // Raw hex data preview
                QStringList hexList;
                for (uint8_t byte : frame.data) {
                    hexList.append(QString("%1").arg(byte, 2, 16, QChar('0')).toUpper());
                }
                return hexList.join(" ");
            }
            default:
                return QVariant();
        }
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() < 6) {
            return QVariant(Qt::AlignCenter);
        }
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    }
    return QVariant();
}

QVariant FrameTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) {
        return QVariant();
    }

    if (orientation == Qt::Horizontal) {
        switch (section) {
            case 0: return "Index";
            case 1: return "Time (s)";
            case 2: return "Delta (s)";
            case 3: return "ID";
            case 4: return "Interface";
            case 5: return "Length";
            case 6: return "Payload Bytes";
            default: return QVariant();
        }
    }
    return QVariant();
}

void FrameTableModel::addFrame(const AetherFrame& frame) {
    beginInsertRows(QModelIndex(), m_frames.count(), m_frames.count());
    m_frames.append(frame);
    endInsertRows();
}

void FrameTableModel::clear() {
    beginResetModel();
    m_frames.clear();
    m_startTime = QDateTime::currentMSecsSinceEpoch();
    endResetModel();
}

AetherFrame FrameTableModel::getFrame(int row) const {
    if (row >= 0 && row < m_frames.count()) {
        return m_frames.at(row);
    }
    return AetherFrame{};
}
