#include "gui/throughput_chart.h"
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <cmath>

namespace aether {

ThroughputChart::ThroughputChart(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(80);
}

void ThroughputChart::setHistory(const QVector<double> &rxHistory, const QVector<double> &txHistory) {
    m_rxHistory = rxHistory;
    m_txHistory = txHistory;
    update();
}

void ThroughputChart::setDarkMode(bool enabled) {
    m_darkMode = enabled;
    update();
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

void ThroughputChart::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();
    const int paddingLeft = 10;
    const int paddingRight = 60;  // Leave room for labels
    const int paddingTop = 15;
    const int paddingBottom = 15;
    const int chartW = w - paddingLeft - paddingRight;
    const int chartH = h - paddingTop - paddingBottom;

    if (chartW <= 0 || chartH <= 0)
        return;

    // Determine max value in history (minimum of 1KB/s scale)
    double maxValue = 1024.0;
    for (double val : m_rxHistory)
        maxValue = std::max(maxValue, val);
    for (double val : m_txHistory)
        maxValue = std::max(maxValue, val);

    // Draw background
    QColor bgColor = m_darkMode ? QColor(30, 30, 30) : QColor(245, 245, 245);
    QColor gridColor = m_darkMode ? QColor(50, 50, 50) : QColor(220, 220, 220);
    QColor textColor = m_darkMode ? QColor(180, 180, 180) : QColor(80, 80, 80);

    painter.fillRect(rect(), bgColor);

    // Draw grid lines (4 horizontal divisions)
    for (int i = 0; i <= 3; ++i) {
        int y = paddingTop + i * chartH / 3;
        painter.setPen(QPen(gridColor, 1, Qt::DashLine));
        painter.drawLine(paddingLeft, y, paddingLeft + chartW, y);

        // Draw rate labels
        double val = maxValue - (i * maxValue / 3.0);
        painter.setPen(textColor);
        painter.drawText(paddingLeft + chartW + 5, y + 4, formatRate(val));
    }

    int n = std::max(m_rxHistory.size(), m_txHistory.size());
    if (n < 2)
        return;

    // Helper lambda to build and draw paths
    auto drawLineAndGradient = [&](const QVector<double> &history, const QColor &lineColor, const QColor &fillColor) {
        if (history.isEmpty())
            return;
        QPainterPath path;
        QPainterPath fillPath;

        double stepX = static_cast<double>(chartW) / (n - 1);

        bool started = false;
        for (int i = 0; i < history.size(); ++i) {
            double x = paddingLeft + i * stepX;
            double val = history[i];
            double y = paddingTop + chartH - (val / maxValue) * chartH;

            // clamp y in chart boundaries just in case
            y = std::max(static_cast<double>(paddingTop), std::min(y, static_cast<double>(paddingTop + chartH)));

            if (!started) {
                path.moveTo(x, y);
                fillPath.moveTo(x, paddingTop + chartH);
                fillPath.lineTo(x, y);
                started = true;
            } else {
                path.lineTo(x, y);
                fillPath.lineTo(x, y);
            }
        }

        if (started) {
            fillPath.lineTo(paddingLeft + (history.size() - 1) * stepX, paddingTop + chartH);
            fillPath.closeSubpath();

            // Fill area
            QLinearGradient gradient(0, paddingTop, 0, paddingTop + chartH);
            gradient.setColorAt(0, fillColor);
            gradient.setColorAt(1, QColor(fillColor.red(), fillColor.green(), fillColor.blue(), 0));
            painter.fillPath(fillPath, gradient);

            // Draw line
            painter.setPen(QPen(lineColor, 2, Qt::SolidLine));
            painter.drawPath(path);
        }
    };

    // Draw RX: Vibrant Cyan/Blue
    drawLineAndGradient(m_rxHistory, QColor(0, 188, 212), QColor(0, 188, 212, 40));

    // Draw TX: Vibrant Orange/Amber
    drawLineAndGradient(m_txHistory, QColor(255, 152, 0), QColor(255, 152, 0, 40));
}

}  // namespace aether
