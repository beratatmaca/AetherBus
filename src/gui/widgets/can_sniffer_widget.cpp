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

    m_autoScrollBtn = new QPushButton(tr("Autoscroll"), this);
    m_autoScrollBtn->setCheckable(true);
    m_autoScrollBtn->setChecked(true);
    m_autoScrollBtn->setToolTip(tr("Keep the most recently received frame in view"));
    controlsLayout->addWidget(m_autoScrollBtn);

    m_pauseBtn = new QPushButton(tr("Pause"), this);
    m_pauseBtn->setCheckable(true);
    m_pauseBtn->setToolTip(tr("Freeze the table; frames keep accumulating and the view catches up on resume"));
    controlsLayout->addWidget(m_pauseBtn);

    controlsLayout->addStretch(1);
    layout->addLayout(controlsLayout);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("CAN ID"), tr("DLC"), tr("Count"), tr("Received"), tr("Data (Hex)")});

    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

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
    if (!m_calc || m_pauseBtn->isChecked()) {
        return;
    }

    auto stats = m_calc->perIdStats();
    QList<quint32> keys = stats.keys();
    // Sort by reception time ascending so the most recently received frame is
    // at the bottom; tie-break by id for a stable order of equal timestamps.
    std::sort(keys.begin(), keys.end(), [&](quint32 a, quint32 b) {
        const qint64 ta = stats[a].lastTimestampMs;
        const qint64 tb = stats[b].lastTimestampMs;
        return ta != tb ? ta < tb : a < b;
    });

    // Rows reorder as traffic arrives, so a row index is no longer tied to a
    // fixed id. Recreate the item/widget objects only when the row count
    // changes; the content of every row is rewritten by position below.
    if (m_table->rowCount() != keys.size()) {
        m_table->setRowCount(keys.size());
        for (int i = 0; i < keys.size(); ++i) {
            for (int col = 0; col < 4; ++col) {
                auto *item = new QTableWidgetItem();
                item->setTextAlignment(Qt::AlignCenter);
                m_table->setItem(i, col, item);
            }

            // Data column
            auto *dataLabel = new QLabel(this);
            dataLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            dataLabel->setContentsMargins(6, 0, 6, 0);
            m_table->setCellWidget(i, 4, dataLabel);
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

        // 3. Received (absolute reception timestamp)
        m_table->item(i, 3)->setText(s.lastTimestampMs == -1
                                         ? QStringLiteral("-")
                                         : QDateTime::fromMSecsSinceEpoch(s.lastTimestampMs).toString(QStringLiteral("HH:mm:ss.zzz")));

        // 4. Data
        auto *dataLabel = qobject_cast<QLabel *>(m_table->cellWidget(i, 4));
        if (dataLabel) {
            dataLabel->setText(formatDataHtml(id, s.lastPayload, now));
        }

        // Color coding for stale rows: if last reception was > 3000 ms ago, gray it out.
        qint64 elapsed = now - s.lastTimestampMs;
        QColor textCol = (s.lastTimestampMs != -1 && elapsed > 3000) ? QColor(128, 128, 128) : palette().color(QPalette::WindowText);
        for (int col = 0; col < 4; ++col) {
            m_table->item(i, col)->setForeground(textCol);
        }
    }

    if (m_autoScrollBtn->isChecked()) {
        m_table->scrollToBottom();
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
