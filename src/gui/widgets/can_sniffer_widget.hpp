#pragma once

#include <QWidget>
#include <QHash>
#include <QVector>
#include <QDateTime>

class QTableWidget;
class QPushButton;

namespace aether {

class StatsCalculator;

/**
 * @brief Live CAN sniffer table: one row per CAN ID with count, age, and payload,
 * highlighting recently changed bytes and graying out stale IDs.
 */
class CanSnifferWidget : public QWidget {
    Q_OBJECT

public:
    explicit CanSnifferWidget(QWidget *parent = nullptr);
    ~CanSnifferWidget() override;

    /** @brief Set the frame-stats source to display (clears the table); nullptr disables refreshes. */
    void setCalculator(StatsCalculator *calc);
    /** @brief Drop all rows and per-byte change tracking. */
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

    /** @brief Per-ID memory of the last payload and when each byte last changed. */
    struct ByteChangeInfo {
        QByteArray prevData;
        QVector<qint64> changeTimestamps;  ///< timestamp of change per byte index
    };
    QHash<quint32, ByteChangeInfo> m_changeTracker;

    // Idle detection: once the calculator's revision stops moving and the
    // time-driven visuals (change highlight, stale-gray) have settled, the
    // 200 ms refresh becomes a no-op.
    quint64 m_lastSeenRevision = 0;
    qint64 m_revisionChangedAtMs = 0;
};

}  // namespace aether
