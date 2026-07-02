#include "core/common/stats_calculator.hpp"

namespace aether {

StatsCalculator::StatsCalculator() {
    m_rxRateHistory.fill(0.0, 60);
    m_txRateHistory.fill(0.0, 60);
}

void StatsCalculator::reset() {
    m_rxBytes = 0;
    m_txBytes = 0;
    m_rxChunks = 0;
    m_txChunks = 0;

    m_lastRxTime = -1;
    m_lastTxTime = -1;
    m_lastChunkTime = -1;
    m_hasLastChunk = false;

    m_rxGap.reset();
    m_txGap.reset();
    m_txRxGap.reset();
    m_rxTxGap.reset();

    m_rxBytesThisPeriod = 0;
    m_txBytesThisPeriod = 0;
    m_currentRxRate = 0.0;
    m_currentTxRate = 0.0;

    m_rxRateHistory.fill(0.0, 60);
    m_txRateHistory.fill(0.0, 60);

    m_perId.clear();
}

void StatsCalculator::addChunk(const CapturedChunk &chunk) {
    const int size = chunk.data.size();
    if (chunk.dir == Direction::Rx) {
        m_rxBytes += size;
        m_rxChunks++;
        m_rxBytesThisPeriod += size;

        if (m_lastRxTime != -1) {
            const qint64 gap = chunk.timestampMs - m_lastRxTime;
            if (gap >= 0) {
                m_rxGap.update(gap);
            }
        }
        if (m_hasLastChunk && m_lastDir == Direction::Tx) {
            const qint64 latency = chunk.timestampMs - m_lastTxTime;
            if (latency >= 0) {
                m_txRxGap.update(latency);
            }
        }
        m_lastRxTime = chunk.timestampMs;
    } else {
        m_txBytes += size;
        m_txChunks++;
        m_txBytesThisPeriod += size;

        if (m_lastTxTime != -1) {
            const qint64 gap = chunk.timestampMs - m_lastTxTime;
            if (gap >= 0) {
                m_txGap.update(gap);
            }
        }
        if (m_hasLastChunk && m_lastDir == Direction::Rx) {
            const qint64 latency = chunk.timestampMs - m_lastRxTime;
            if (latency >= 0) {
                m_rxTxGap.update(latency);
            }
        }
        m_lastTxTime = chunk.timestampMs;
    }

    m_lastDir = chunk.dir;
    m_lastChunkTime = chunk.timestampMs;
    m_hasLastChunk = true;

    if (chunk.isFrame) {
        CanIdStat &s = m_perId[chunk.frameId];
        if (s.lastTimestampMs != -1) {
            const qint64 gap = chunk.timestampMs - s.lastTimestampMs;
            if (gap >= 0) {
                s.gap.update(gap);
            }
        }
        s.count++;
        s.lastTimestampMs = chunk.timestampMs;
        s.lastFlags = chunk.frameFlags;
        s.lastLen = size;
        s.lastPayload = chunk.data;
    }
}

void StatsCalculator::setSerialConfig(int baud, int dataBits, aether::Parity parity, int stopBits) {
    m_baud = baud;
    m_dataBits = dataBits;
    m_parity = parity;
    m_stopBits = stopBits;
}

void StatsCalculator::rollRates() {
    m_currentRxRate = static_cast<double>(m_rxBytesThisPeriod);
    m_currentTxRate = static_cast<double>(m_txBytesThisPeriod);
    m_rxBytesThisPeriod = 0;
    m_txBytesThisPeriod = 0;

    m_rxRateHistory.append(m_currentRxRate);
    m_txRateHistory.append(m_currentTxRate);

    if (m_rxRateHistory.size() > 60) {
        m_rxRateHistory.removeFirst();
    }
    if (m_txRateHistory.size() > 60) {
        m_txRateHistory.removeFirst();
    }
}

double StatsCalculator::rxBaudUtilization() const {
    if (m_baud <= 0)
        return 0.0;
    const double bitsPerChar = 1.0 + m_dataBits + (m_parity == Parity::None ? 0.0 : 1.0) + m_stopBits;
    const double maxBytesPerSec = static_cast<double>(m_baud) / bitsPerChar;
    if (maxBytesPerSec <= 0.0)
        return 0.0;
    return (m_currentRxRate / maxBytesPerSec) * 100.0;
}

double StatsCalculator::txBaudUtilization() const {
    if (m_baud <= 0)
        return 0.0;
    const double bitsPerChar = 1.0 + m_dataBits + (m_parity == Parity::None ? 0.0 : 1.0) + m_stopBits;
    const double maxBytesPerSec = static_cast<double>(m_baud) / bitsPerChar;
    if (maxBytesPerSec <= 0.0)
        return 0.0;
    return (m_currentTxRate / maxBytesPerSec) * 100.0;
}

}  // namespace aether
