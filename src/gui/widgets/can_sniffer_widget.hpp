#pragma once

#include <QWidget>
#include <QHash>
#include <QVector>
#include <QDateTime>

class QTableWidget;
class QPushButton;

namespace aether {

class StatsCalculator;

class CanSnifferWidget : public QWidget {
    Q_OBJECT

public:
    explicit CanSnifferWidget(QWidget *parent = nullptr);
    ~CanSnifferWidget() override;

    void setCalculator(StatsCalculator *calc);
    void clearSniffer();

private slots:
    void refreshUi();

private:
    void buildUi();
    QString formatDataHtml(quint32 id, const QByteArray &data, qint64 currentTimeMs);

    StatsCalculator *m_calc = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_autoScrollBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;

    struct ByteChangeInfo {
        QByteArray prevData;
        QVector<qint64> changeTimestamps; // timestamp of change per byte index
    };
    QHash<quint32, ByteChangeInfo> m_changeTracker;
};

}  // namespace aether
