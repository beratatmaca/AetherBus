#pragma once

#include <QWidget>
#include <QVector>

namespace aether {

class ThroughputChart : public QWidget {
    Q_OBJECT
public:
    explicit ThroughputChart(QWidget *parent = nullptr);

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
