#include "gui/widgets/can_sniffer_widget.hpp"
#include "core/common/stats_calculator.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <algorithm>

namespace aether {

CanSnifferWidget::CanSnifferWidget(QWidget *parent) : QWidget(parent) {
    buildUi();

    auto *timer = new QTimer(this);
    timer->setInterval(200);
    connect(timer, &QTimer::timeout, this, &CanSnifferWidget::refreshUi);
    timer->start();
}

CanSnifferWidget::~CanSnifferWidget() = default;

void CanSnifferWidget::buildUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    // Controls row
    auto *controlsLayout = new QHBoxLayout();
    m_clearBtn = new QPushButton(tr("Clear Sniffer"), this);
    connect(m_clearBtn, &QPushButton::clicked, this, &CanSnifferWidget::clearSniffer);
    controlsLayout->addWidget(m_clearBtn);
    controlsLayout->addStretch(1);
    layout->addLayout(controlsLayout);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({tr("CAN ID"), tr("DLC"), tr("Count"), tr("Period"), tr("Last Seen"), tr("Data (Hex)")});

    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);

    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);

    layout->addWidget(m_table, 1);
}

void CanSnifferWidget::setCalculator(StatsCalculator *calc) {
    m_calc = calc;
    clearSniffer();
}

void CanSnifferWidget::clearSniffer() {
    m_table->setRowCount(0);
    m_changeTracker.clear();
}

void CanSnifferWidget::refreshUi() {
    if (!m_calc) {
        return;
    }

    auto stats = m_calc->perIdStats();
    QList<quint32> keys = stats.keys();
    std::sort(keys.begin(), keys.end());

    // Check if we need to rebuild the row structures
    bool rebuild = (m_table->rowCount() != keys.size());
    if (!rebuild) {
        for (int i = 0; i < keys.size(); ++i) {
            if (m_table->item(i, 0)->data(Qt::UserRole).toUInt() != keys[i]) {
                rebuild = true;
                break;
            }
        }
    }

    if (rebuild) {
        m_table->setRowCount(keys.size());
        for (int i = 0; i < keys.size(); ++i) {
            auto *idItem = new QTableWidgetItem();
            idItem->setData(Qt::UserRole, keys[i]);
            idItem->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(i, 0, idItem);

            auto *dlcItem = new QTableWidgetItem();
            dlcItem->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(i, 1, dlcItem);

            auto *countItem = new QTableWidgetItem();
            countItem->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(i, 2, countItem);

            auto *periodItem = new QTableWidgetItem();
            periodItem->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(i, 3, periodItem);

            auto *lastSeenItem = new QTableWidgetItem();
            lastSeenItem->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(i, 4, lastSeenItem);

            // Data column
            auto *dataLabel = new QLabel(this);
            dataLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            dataLabel->setContentsMargins(6, 0, 6, 0);
            m_table->setCellWidget(i, 5, dataLabel);
        }
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < keys.size(); ++i) {
        quint32 id = keys[i];
        const auto &s = stats[id];

        // 0. ID
        QString idStr = QStringLiteral("0x") + QString::number(id, 16).toUpper();
        if (s.lastFlags & FrameExtendedId) {
            idStr += QStringLiteral(" (ext)");
        }
        m_table->item(i, 0)->setText(idStr);

        // 1. DLC
        m_table->item(i, 1)->setText(QString::number(s.lastLen));

        // 2. Count
        m_table->item(i, 2)->setText(QString::number(s.count));

        // 3. Period
        m_table->item(i, 3)->setText(s.gap.count > 0 ? QString("%1 ms").arg(s.gap.avg(), 0, 'f', 1) : QStringLiteral("-"));

        // 4. Last Seen
        qint64 elapsed = now - s.lastTimestampMs;
        QString elapsedStr;
        if (s.lastTimestampMs == -1) {
            elapsedStr = QStringLiteral("-");
        } else if (elapsed < 1000) {
            elapsedStr = QString("%1 ms ago").arg(elapsed);
        } else {
            elapsedStr = QString("%1 s ago").arg(elapsed / 1000.0, 0, 'f', 1);
        }
        m_table->item(i, 4)->setText(elapsedStr);

        // 5. Data
        auto *dataLabel = qobject_cast<QLabel *>(m_table->cellWidget(i, 5));
        if (dataLabel) {
            dataLabel->setText(formatDataHtml(id, s.lastPayload, now));
        }

        // Color coding for stale rows: if elapsed > 3000 ms, show in gray
        QColor textCol = (elapsed > 3000) ? QColor(128, 128, 128) : palette().color(QPalette::WindowText);
        for (int col = 0; col < 5; ++col) {
            m_table->item(i, col)->setForeground(textCol);
        }
    }
}

QString CanSnifferWidget::formatDataHtml(quint32 id, const QByteArray &data, qint64 currentTimeMs) {
    if (data.isEmpty()) {
        return QStringLiteral("<i style='color:gray;'>no data (RTR)</i>");
    }

    auto &tracker = m_changeTracker[id];
    int len = data.size();

    // Resize timestamps vector if size changed
    if (tracker.changeTimestamps.size() != len) {
        tracker.changeTimestamps.resize(len);
        tracker.changeTimestamps.fill(0);
    }

    // Compare and update change timestamps
    for (int i = 0; i < len; ++i) {
        if (i >= tracker.prevData.size() || data[i] != tracker.prevData[i]) {
            tracker.changeTimestamps[i] = currentTimeMs;
        }
    }
    tracker.prevData = data;

    // Build HTML representation
    QString html = QStringLiteral("<code style='font-family: monospace;'>");
    for (int i = 0; i < len; ++i) {
        if (i > 0) {
            html += QStringLiteral(" ");
        }
        QString byteHex = QString::asprintf("%02X", static_cast<unsigned char>(data[i]));
        qint64 age = currentTimeMs - tracker.changeTimestamps[i];
        if (age < 1000) {
            // Highlight color - bright red
            html += QStringLiteral("<span style='color:#e57373; font-weight:bold;'>%1</span>").arg(byteHex);
        } else {
            html += byteHex;
        }
    }
    html += QStringLiteral("</code>");
    return html;
}

}  // namespace aether
