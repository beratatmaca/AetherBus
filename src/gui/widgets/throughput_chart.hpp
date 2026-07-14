#pragma once

#include <QWidget>
#include <QVector>

namespace aether {

/** @brief Custom-painted line chart of recent Rx/Tx throughput (bytes/s per sample). */
class ThroughputChart : public QWidget {
    Q_OBJECT
public:
    explicit ThroughputChart(QWidget *parent = nullptr);

    /** @brief Replace the plotted Rx/Tx rate histories and repaint. */
    void setHistory(const QVector<double> &rxHistory, const QVector<double> &txHistory);
    void setDarkMode(bool enabled);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_rxHistory;
    QVector<double> m_txHistory;
    bool m_darkMode = false;
};

}  // namespace aether
