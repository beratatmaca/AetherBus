#pragma once

#include "core/serial/serial_types.hpp"
#include <QHash>
#include <QVector>
#include <QByteArray>

class QJsonObject;

namespace aether {

struct StatValue {
    qint64 min = -1;
    qint64 max = -1;
    qint64 sum = 0;
    qint64 count = 0;

    void update(qint64 val) {
        if (min == -1 || val < min)
            min = val;
        if (max == -1 || val > max)
            max = val;
        sum += val;
        count++;
    }

    [[nodiscard]] double avg() const { return count > 0 ? static_cast<double>(sum) / count : 0.0; }

    void reset() {
        min = -1;
        max = -1;
        sum = 0;
        count = 0;
    }
};

/** @brief Per-identifier statistics for framed transports (CAN), accumulated purely from observed traffic. */
struct CanIdStat {
    quint64 count = 0;            ///< Frames seen with this id.
    qint64 lastTimestampMs = -1;  ///< Timestamp of the most recent frame.
    StatValue gap;                ///< Inter-frame gap for this id (ms).
    quint16 lastFlags = 0;        ///< Flags of the most recent frame.
    int lastLen = 0;              ///< Payload length of the most recent frame.
    QByteArray lastPayload;       ///< Raw payload of the most recent frame.
};

class StatsCalculator {
public:
    StatsCalculator();

    void addChunk(const CapturedChunk &chunk);
    void reset();

    // Configuration for baud rate utilization
    void setSerialConfig(int baud, int dataBits, aether::Parity parity, int stopBits);

    // Getters
    qint64 rxBytes() const { return m_rxBytes; }
    qint64 txBytes() const { return m_txBytes; }
    qint64 rxChunks() const { return m_rxChunks; }
    qint64 txChunks() const { return m_txChunks; }

    const StatValue &rxGap() const { return m_rxGap; }
    const StatValue &txGap() const { return m_txGap; }
    const StatValue &txRxGap() const { return m_txRxGap; }
    const StatValue &rxTxGap() const { return m_rxTxGap; }

    // Roll rates and get current rates
    void rollRates();
    double currentRxRate() const { return m_currentRxRate; }
    double currentTxRate() const { return m_currentTxRate; }
    double rxBaudUtilization() const;
    double txBaudUtilization() const;

    const QVector<double> &rxRateHistory() const { return m_rxRateHistory; }
    const QVector<double> &txRateHistory() const { return m_txRateHistory; }

    /** @brief Per-CAN-id statistics keyed by identifier (populated only for framed chunks). */
    const QHash<quint32, CanIdStat> &perIdStats() const { return m_perId; }

    /**
     * @brief Monotonic change counter, bumped by addChunk()/rollRates()/reset().
     *
     * Periodic UI refreshers compare it against their last-seen value so an
     * idle bus costs them nothing.
     */
    quint64 revision() const { return m_revision; }

private:
    qint64 m_rxBytes = 0;
    qint64 m_txBytes = 0;
    qint64 m_rxChunks = 0;
    qint64 m_txChunks = 0;

    // Timing helper states
    qint64 m_lastRxTime = -1;
    qint64 m_lastTxTime = -1;
    qint64 m_lastChunkTime = -1;
    Direction m_lastDir = Direction::Rx;
    bool m_hasLastChunk = false;

    // Gaps
    StatValue m_rxGap;
    StatValue m_txGap;
    StatValue m_txRxGap;  ///< Tx -> Rx latency
    StatValue m_rxTxGap;  ///< Rx -> Tx latency

    // Rates calculation
    qint64 m_rxBytesThisPeriod = 0;
    qint64 m_txBytesThisPeriod = 0;
    double m_currentRxRate = 0.0;  ///< bytes/sec
    double m_currentTxRate = 0.0;  ///< bytes/sec

    QVector<double> m_rxRateHistory;
    QVector<double> m_txRateHistory;

    QHash<quint32, CanIdStat> m_perId;

    quint64 m_revision = 0;

    // Serial parameters
    int m_baud = 115200;
    int m_dataBits = 8;
    aether::Parity m_parity = Parity::None;
    int m_stopBits = 1;
};

/**
 * @brief Snapshot a calculator's counters as a control-channel JSON object.
 *
 * Shared by every session type's `stats` control verb so the wire format stays
 * identical. Produces `{rxBytes, txBytes, rxChunks, txChunks, rxRate, txRate,
 * running}` (rates in bytes/sec).
 */
QJsonObject statsToControlJson(const StatsCalculator &stats, bool running);

}  // namespace aether
