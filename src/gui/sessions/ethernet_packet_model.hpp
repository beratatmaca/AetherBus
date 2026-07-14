#pragma once

#include "core/serial/serial_types.hpp"

#include <QAbstractTableModel>
#include <QVector>

#include <cstdint>
#include <deque>

namespace aether {

/**
 * @brief Table model backing the Ethernet session's Wireshark-style packet list.
 *
 * Backed by a std::deque (not QVector/QTableWidget) so evicting the oldest
 * packet once @ref kMaxRows is exceeded is O(1) amortized rather than
 * shifting every remaining row, and colours rows by direction (matching
 * ConsoleView's Rx/Tx convention) via Qt::ForegroundRole.
 */
class EthernetPacketModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column : std::uint8_t { ColTime = 0, ColDir, ColSource, ColDestination, ColProtocol, ColLength, ColCount };

    explicit EthernetPacketModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /** @brief Append a captured packet, evicting the oldest once @ref kMaxRows is exceeded. */
    void appendPacket(const CapturedChunk &chunk);

    /**
     * @brief Append a whole drain-tick's worth of packets with one insert
     * notification (and at most one eviction notification), instead of a
     * begin/endInsertRows round-trip per packet.
     */
    void appendPackets(const QVector<CapturedChunk> &chunks);

    /** @brief Remove all rows. */
    void clearPackets();

    /**
     * @brief Raw captured chunk backing @p row.
     *
     * @p row must be in [0, rowCount()).
     */
    [[nodiscard]] const CapturedChunk &chunkAt(int row) const;

    [[nodiscard]] int packetCount() const { return static_cast<int>(m_rows.size()); }

    static constexpr int kMaxRows = 10000;  ///< Ceiling matching ConsoleView::kMaxLines, so a long-running or
                                            ///< high-rate capture doesn't grow memory/table rows without bound.

private:
    struct Row {
        CapturedChunk chunk;
        QString source;
        QString destination;
        QString protocol;
    };

    static Row summarize(const CapturedChunk &chunk);

    std::deque<Row> m_rows;
};

}  // namespace aether
