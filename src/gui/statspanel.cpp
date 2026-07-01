#include "gui/statspanel.h"
#include "gui/throughput_chart.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <QScrollArea>
#include <QPalette>
#include <QApplication>

namespace aether {

StatsPanel::StatsPanel(QWidget *parent) : QWidget(parent) {
    m_activeCalc = &m_dummyCalc;

    buildUi();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(200);
    connect(m_refreshTimer, &QTimer::timeout, this, &StatsPanel::refreshUi);
    m_refreshTimer->start();

    m_oneSecondTimer = new QTimer(this);
    m_oneSecondTimer->setInterval(1000);
    connect(m_oneSecondTimer, &QTimer::timeout, this, &StatsPanel::oneSecondTick);
    m_oneSecondTimer->start();
}

StatsPanel::~StatsPanel() = default;

void StatsPanel::buildUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scrollArea);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // 1. Throughput rates group box
    auto *ratesGroup = new QGroupBox(tr("Throughput & Packet Rates"), content);
    auto *ratesLayout = new QGridLayout(ratesGroup);
    ratesLayout->setSpacing(4);
    ratesLayout->setContentsMargins(4, 4, 4, 4);

    // Headers
    auto *dirHeader = new QLabel(tr("Dir"), ratesGroup);
    auto *rateHeader = new QLabel(tr("Speed"), ratesGroup);
    auto *pktHeader = new QLabel(tr("Packets"), ratesGroup);
    auto *utilHeader = new QLabel(tr("Baud Util"), ratesGroup);

    for (QLabel *lbl : {dirHeader, rateHeader, pktHeader, utilHeader}) {
        QFont f = lbl->font();
        f.setBold(true);
        lbl->setFont(f);
    }

    ratesLayout->addWidget(dirHeader, 0, 0);
    ratesLayout->addWidget(rateHeader, 0, 1);
    ratesLayout->addWidget(pktHeader, 0, 2);
    ratesLayout->addWidget(utilHeader, 0, 3);

    // Rx labels
    auto *rxLabel = new QLabel(tr("Rx:"), ratesGroup);
    rxLabel->setStyleSheet("color: #00bcd4; font-weight: bold;");  // Cyan
    m_rxRateLbl = new QLabel("0 B/s", ratesGroup);
    m_rxPktLbl = new QLabel("0 p/s", ratesGroup);
    m_rxUtilLbl = new QLabel("0.0%", ratesGroup);

    ratesLayout->addWidget(rxLabel, 1, 0);
    ratesLayout->addWidget(m_rxRateLbl, 1, 1);
    ratesLayout->addWidget(m_rxPktLbl, 1, 2);
    ratesLayout->addWidget(m_rxUtilLbl, 1, 3);

    // Tx labels
    auto *txLabel = new QLabel(tr("Tx:"), ratesGroup);
    txLabel->setStyleSheet("color: #ff9800; font-weight: bold;");  // Orange
    m_txRateLbl = new QLabel("0 B/s", ratesGroup);
    m_txPktLbl = new QLabel("0 p/s", ratesGroup);
    m_txUtilLbl = new QLabel("0.0%", ratesGroup);

    ratesLayout->addWidget(txLabel, 2, 0);
    ratesLayout->addWidget(m_txRateLbl, 2, 1);
    ratesLayout->addWidget(m_txPktLbl, 2, 2);
    ratesLayout->addWidget(m_txUtilLbl, 2, 3);

    layout->addWidget(ratesGroup);

    // 2. Chart group box
    auto *chartGroup = new QGroupBox(tr("Throughput History"), content);
    auto *chartLayout = new QVBoxLayout(chartGroup);
    chartLayout->setContentsMargins(2, 2, 2, 2);
    m_chart = new ThroughputChart(chartGroup);
    chartLayout->addWidget(m_chart);
    layout->addWidget(chartGroup);

    // 3. Gap analysis group box
    auto *gapGroup = new QGroupBox(tr("Inter-Packet Gap & Latency"), content);
    auto *gapLayout = new QGridLayout(gapGroup);
    gapLayout->setSpacing(4);
    gapLayout->setContentsMargins(4, 4, 4, 4);

    auto *gapHdrType = new QLabel(tr("Metric"), gapGroup);
    auto *gapHdrMin = new QLabel(tr("Min"), gapGroup);
    auto *gapHdrMax = new QLabel(tr("Max"), gapGroup);
    auto *gapHdrAvg = new QLabel(tr("Avg"), gapGroup);

    for (QLabel *lbl : {gapHdrType, gapHdrMin, gapHdrMax, gapHdrAvg}) {
        QFont f = lbl->font();
        f.setBold(true);
        lbl->setFont(f);
    }

    gapLayout->addWidget(gapHdrType, 0, 0);
    gapLayout->addWidget(gapHdrMin, 0, 1);
    gapLayout->addWidget(gapHdrMax, 0, 2);
    gapLayout->addWidget(gapHdrAvg, 0, 3);

    // Rx -> Rx
    gapLayout->addWidget(new QLabel(tr("Rx -> Rx"), gapGroup), 1, 0);
    m_rxGapMin = new QLabel("-", gapGroup);
    m_rxGapMax = new QLabel("-", gapGroup);
    m_rxGapAvg = new QLabel("-", gapGroup);
    gapLayout->addWidget(m_rxGapMin, 1, 1);
    gapLayout->addWidget(m_rxGapMax, 1, 2);
    gapLayout->addWidget(m_rxGapAvg, 1, 3);

    // Tx -> Tx
    gapLayout->addWidget(new QLabel(tr("Tx -> Tx"), gapGroup), 2, 0);
    m_txGapMin = new QLabel("-", gapGroup);
    m_txGapMax = new QLabel("-", gapGroup);
    m_txGapAvg = new QLabel("-", gapGroup);
    gapLayout->addWidget(m_txGapMin, 2, 1);
    gapLayout->addWidget(m_txGapMax, 2, 2);
    gapLayout->addWidget(m_txGapAvg, 2, 3);

    // Tx -> Rx (Response)
    gapLayout->addWidget(new QLabel(tr("Tx -> Rx"), gapGroup), 3, 0);
    m_txRxMin = new QLabel("-", gapGroup);
    m_txRxMax = new QLabel("-", gapGroup);
    m_txRxAvg = new QLabel("-", gapGroup);
    gapLayout->addWidget(m_txRxMin, 3, 1);
    gapLayout->addWidget(m_txRxMax, 3, 2);
    gapLayout->addWidget(m_txRxAvg, 3, 3);

    // Rx -> Tx
    gapLayout->addWidget(new QLabel(tr("Rx -> Tx"), gapGroup), 4, 0);
    m_rxTxMin = new QLabel("-", gapGroup);
    m_rxTxMax = new QLabel("-", gapGroup);
    m_rxTxAvg = new QLabel("-", gapGroup);
    gapLayout->addWidget(m_rxTxMin, 4, 1);
    gapLayout->addWidget(m_rxTxMax, 4, 2);
    gapLayout->addWidget(m_rxTxAvg, 4, 3);

    layout->addWidget(gapGroup);

    // 4. Control buttons
    m_resetBtn = new QPushButton(tr("Reset Statistics"), content);
    connect(m_resetBtn, &QPushButton::clicked, this, &StatsPanel::resetStats);
    layout->addWidget(m_resetBtn);

    layout->addStretch(1);

    scrollArea->setWidget(content);
    mainLayout->addWidget(scrollArea);
}

void StatsPanel::setActiveCalculator(StatsCalculator *calc) {
    if (calc) {
        m_activeCalc = calc;
    } else {
        m_activeCalc = &m_dummyCalc;
    }
    m_chart->setHistory(m_activeCalc->rxRateHistory(), m_activeCalc->txRateHistory());

    // Reset our local packet rate tracking when switching calculators
    m_rxChunksThisSecond = 0;
    m_txChunksThisSecond = 0;
    m_currentRxPktRate = 0.0;
    m_currentTxPktRate = 0.0;

    refreshUi();
}

void StatsPanel::resetStats() {
    m_activeCalc->reset();
    m_rxChunksThisSecond = 0;
    m_txChunksThisSecond = 0;
    m_currentRxPktRate = 0.0;
    m_currentTxPktRate = 0.0;
    m_chart->setHistory(m_activeCalc->rxRateHistory(), m_activeCalc->txRateHistory());
    refreshUi();
}

QString StatsPanel::formatGap(qint64 ms) const {
    if (ms < 0)
        return QStringLiteral("-");
    return QString("%1 ms").arg(ms);
}

static QString formatRate(double bytesPerSec) {
    if (bytesPerSec >= 1024.0 * 1024.0) {
        return QString::number(bytesPerSec / (1024.0 * 1024.0), 'f', 1) + " MB/s";
    }
    if (bytesPerSec >= 1024.0) {
        return QString::number(bytesPerSec / 1024.0, 'f', 1) + " KB/s";
    }
    return QString::number(bytesPerSec, 'f', 0) + " B/s";
}

void StatsPanel::refreshUi() {
    if (!m_activeCalc)
        return;

    // Determine dark mode state from current palette
    bool isDark = palette().color(QPalette::WindowText).lightness() > palette().color(QPalette::Window).lightness();
    m_chart->setDarkMode(isDark);

    // Update rate labels
    m_rxRateLbl->setText(formatRate(m_activeCalc->currentRxRate()));
    m_rxPktLbl->setText(QString("%1 p/s").arg(m_currentRxPktRate, 0, 'f', 0));
    m_rxUtilLbl->setText(QString("%1%").arg(m_activeCalc->rxBaudUtilization(), 0, 'f', 1));

    m_txRateLbl->setText(formatRate(m_activeCalc->currentTxRate()));
    m_txPktLbl->setText(QString("%1 p/s").arg(m_currentTxPktRate, 0, 'f', 0));
    m_txUtilLbl->setText(QString("%1%").arg(m_activeCalc->txBaudUtilization(), 0, 'f', 1));

    // Update gap labels
    const auto &rxG = m_activeCalc->rxGap();
    m_rxGapMin->setText(formatGap(rxG.min));
    m_rxGapMax->setText(formatGap(rxG.max));
    m_rxGapAvg->setText(rxG.count > 0 ? QString("%1 ms").arg(rxG.avg(), 0, 'f', 1) : QStringLiteral("-"));

    const auto &txG = m_activeCalc->txGap();
    m_txGapMin->setText(formatGap(txG.min));
    m_txGapMax->setText(formatGap(txG.max));
    m_txGapAvg->setText(txG.count > 0 ? QString("%1 ms").arg(txG.avg(), 0, 'f', 1) : QStringLiteral("-"));

    const auto &txRxG = m_activeCalc->txRxGap();
    m_txRxMin->setText(formatGap(txRxG.min));
    m_txRxMax->setText(formatGap(txRxG.max));
    m_txRxAvg->setText(txRxG.count > 0 ? QString("%1 ms").arg(txRxG.avg(), 0, 'f', 1) : QStringLiteral("-"));

    const auto &rxTxG = m_activeCalc->rxTxGap();
    m_rxTxMin->setText(formatGap(rxTxG.min));
    m_rxTxMax->setText(formatGap(rxTxG.max));
    m_rxTxAvg->setText(rxTxG.count > 0 ? QString("%1 ms").arg(rxTxG.avg(), 0, 'f', 1) : QStringLiteral("-"));
}

void StatsPanel::oneSecondTick() {
    if (!m_activeCalc)
        return;

    // We get packet counts from the active calculator's chunks count difference
    static qint64 lastRxChunks = 0;
    static qint64 lastTxChunks = 0;

    qint64 currentRxChunks = m_activeCalc->rxChunks();
    qint64 currentTxChunks = m_activeCalc->txChunks();

    // Handle resets
    if (currentRxChunks < lastRxChunks)
        lastRxChunks = 0;
    if (currentTxChunks < lastTxChunks)
        lastTxChunks = 0;

    m_currentRxPktRate = static_cast<double>(currentRxChunks - lastRxChunks);
    m_currentTxPktRate = static_cast<double>(currentTxChunks - lastTxChunks);

    lastRxChunks = currentRxChunks;
    lastTxChunks = currentTxChunks;

    m_chart->setHistory(m_activeCalc->rxRateHistory(), m_activeCalc->txRateHistory());
}

}  // namespace aether
