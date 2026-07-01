#pragma once

#include <QWidget>
#include "core/common/stats_calculator.hpp"

class QLabel;
class QTimer;
class QPushButton;

namespace aether {

class ThroughputChart;

class StatsPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatsPanel(QWidget *parent = nullptr);
    ~StatsPanel() override;

    void setActiveCalculator(StatsCalculator *calc);
    void resetStats();

private slots:
    void refreshUi();
    void oneSecondTick();

private:
    void buildUi();
    QString formatGap(qint64 ms) const;

    StatsCalculator *m_activeCalc = nullptr;
    StatsCalculator m_dummyCalc;  // Used when no active calculator is set
    QTimer *m_refreshTimer = nullptr;
    QTimer *m_oneSecondTimer = nullptr;

    // Rates UI Labels
    QLabel *m_rxRateLbl = nullptr;
    QLabel *m_rxPktLbl = nullptr;
    QLabel *m_rxUtilLbl = nullptr;
    QLabel *m_txRateLbl = nullptr;
    QLabel *m_txPktLbl = nullptr;
    QLabel *m_txUtilLbl = nullptr;

    // Gaps UI Labels (Min, Max, Avg)
    QLabel *m_rxGapMin = nullptr;
    QLabel *m_rxGapMax = nullptr;
    QLabel *m_rxGapAvg = nullptr;

    QLabel *m_txGapMin = nullptr;
    QLabel *m_txGapMax = nullptr;
    QLabel *m_txGapAvg = nullptr;

    QLabel *m_txRxMin = nullptr;
    QLabel *m_txRxMax = nullptr;
    QLabel *m_txRxAvg = nullptr;

    QLabel *m_rxTxMin = nullptr;
    QLabel *m_rxTxMax = nullptr;
    QLabel *m_rxTxAvg = nullptr;

    // Throughput Chart
    ThroughputChart *m_chart = nullptr;

    QPushButton *m_resetBtn = nullptr;

    // Packet rates this second tracking
    qint64 m_rxChunksThisSecond = 0;
    qint64 m_txChunksThisSecond = 0;
    double m_currentRxPktRate = 0.0;
    double m_currentTxPktRate = 0.0;
};

}  // namespace aether
