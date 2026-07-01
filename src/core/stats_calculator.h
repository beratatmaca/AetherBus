#pragma once

#include "core/serial_types.h"
#include <QVector>

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
    StatValue m_txRxGap;  // Tx -> Rx latency
    StatValue m_rxTxGap;  // Rx -> Tx latency

    // Rates calculation
    qint64 m_rxBytesThisPeriod = 0;
    qint64 m_txBytesThisPeriod = 0;
    double m_currentRxRate = 0.0;  // bytes/sec
    double m_currentTxRate = 0.0;  // bytes/sec

    QVector<double> m_rxRateHistory;
    QVector<double> m_txRateHistory;

    // Serial parameters
    int m_baud = 115200;
    int m_dataBits = 8;
    aether::Parity m_parity = Parity::None;
    int m_stopBits = 1;
};

}  // namespace aether
