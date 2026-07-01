#include "gui/widgets/consoleview.hpp"
#include <QPainter>
#include <QScrollBar>

namespace aether {

// Row backgrounds
static const QColor kRxBg(22, 28, 26);
static const QColor kTxBg(28, 26, 22);

// Foreground text
static const QColor kRxFg(128, 222, 234);  // Cyan
static const QColor kTxFg(255, 171, 64);   // Orange
static const QColor kMetaFg(110, 110, 120);

// Selection / Search highlights
static const QColor kSelBg(48, 63, 159, 128);     // Semi-transparent Indigo
static const QColor kSearchBg(255, 235, 59, 80);  // Semi-transparent Yellow

void ConsoleView::paintEvent(QPaintEvent * /*event*/) {
    QPainter p(viewport());
    p.setFont(m_font);

    const int hOff = horizontalScrollBar()->value();
    const int vOff = verticalScrollBar()->value();
    const int viewW = viewport()->width();
    const int viewH = viewport()->height();

    // Fill background.
    p.fillRect(0, 0, viewW, viewH, QColor(18, 18, 24));

    if (m_lines.isEmpty()) {
        return;
    }

    const int firstLine = vOff / m_lineH;
    const int lastLine = qMin(m_lines.size() - 1, firstLine + viewH / m_lineH + 1);

    for (int li = firstLine; li <= lastLine; ++li) {
        const DisplayLine &dl = m_lines.at(li);
        const bool isRx = (dl.dir == Direction::Rx);

        const int yTop = li * m_lineH - vOff;
        const int yBase = yTop + m_fontAscent + 1;

        // Row background — subtle tint.
        p.fillRect(0, yTop, viewW, m_lineH, isRx ? kRxBg : kTxBg);

        int x = kLeftPad - hOff;

        p.setPen(kMetaFg);
        p.drawText(x, yBase, dl.prefix);
        x += (dl.prefix.length() + 1) * m_charW;

        const QColor &fgColor = isRx ? kRxFg : kTxFg;
        int numCols = dl.cols.size();
        int numBytes = dl.bytes.size();

        if (numCols > 0) {
            for (int bi = 0; bi < numBytes; ++bi) {
                // Calculate dynamic cell width based on active columns and their badges
                int cellW = 0;
                for (int ci = 0; ci < numCols; ++ci) {
                    int formatType = -1;
                    int activeCount = 0;
                    if (m_showHex) {
                        if (activeCount == ci) {
                            formatType = 0;
                        }
                        activeCount++;
                    }
                    if (m_showDec) {
                        if (activeCount == ci) {
                            formatType = 1;
                        }
                        activeCount++;
                    }
                    if (m_showBin) {
                        if (activeCount == ci) {
                            formatType = 2;
                        }
                        activeCount++;
                    }

                    int tokenLen = dl.cols.at(ci).at(bi).length();
                    if (formatType == 0) {
                        cellW += tokenLen * m_charW;
                    } else {
                        cellW += tokenLen * m_charW + 6;
                    }
                    if (ci < numCols - 1) {
                        cellW += 4;
                    }
                }

                for (const SearchHit &hit : m_searchHits) {
                    if (hit.line != li) {
                        continue;
                    }
                    const int prefixLen = dl.prefix.length() + 1;
                    int charOff = prefixLen;
                    for (int j = 0; j < bi; ++j) {
                        QStringList prevParts;
                        for (int ci = 0; ci < numCols; ++ci) {
                            if (j < dl.cols.at(ci).size()) {
                                prevParts << dl.cols.at(ci).at(j);
                            }
                        }
                        charOff += prevParts.join(QLatin1Char('/')).length() + 1;
                    }
                    if (hit.start <= charOff && charOff < hit.start + hit.len) {
                        p.fillRect(x, yTop, cellW, m_lineH, kSearchBg);
                    }
                }

                if (m_hasSelection) {
                    const CursorPos selLo =
                        (m_selAnchor.line < m_selEnd.line || (m_selAnchor.line == m_selEnd.line && m_selAnchor.column <= m_selEnd.column))
                            ? m_selAnchor
                            : m_selEnd;
                    const CursorPos selHi =
                        (m_selAnchor.line < m_selEnd.line || (m_selAnchor.line == m_selEnd.line && m_selAnchor.column <= m_selEnd.column))
                            ? m_selEnd
                            : m_selAnchor;

                    const bool lineInSel = li > selLo.line && li < selHi.line;
                    const bool lineIsLo = li == selLo.line;
                    const bool lineIsHi = li == selHi.line;

                    if (lineInSel) {
                        p.fillRect(x, yTop, cellW + m_charW, m_lineH, kSelBg);
                    } else if (lineIsLo && lineIsHi) {
                        const int prefixLen = dl.prefix.length() + 1;
                        int charOff = prefixLen;
                        for (int j = 0; j < bi; ++j) {
                            QStringList pp;
                            for (int ci = 0; ci < numCols; ++ci) {
                                if (j < dl.cols.at(ci).size())
                                    pp << dl.cols.at(ci).at(j);
                            }
                            charOff += pp.join(QLatin1Char('/')).length() + 1;
                        }
                        const int cellEnd = charOff + (cellW / m_charW);
                        if (cellEnd > selLo.column && charOff < selHi.column) {
                            p.fillRect(x, yTop, cellW, m_lineH, kSelBg);
                        }
                    } else if (lineIsLo) {
                        const int prefixLen = dl.prefix.length() + 1;
                        int charOff = prefixLen;
                        for (int j = 0; j < bi; ++j) {
                            QStringList pp;
                            for (int ci = 0; ci < numCols; ++ci) {
                                if (j < dl.cols.at(ci).size())
                                    pp << dl.cols.at(ci).at(j);
                            }
                            charOff += pp.join(QLatin1Char('/')).length() + 1;
                        }
                        if (charOff >= selLo.column) {
                            p.fillRect(x, yTop, cellW, m_lineH, kSelBg);
                        }
                    } else if (lineIsHi) {
                        const int prefixLen = dl.prefix.length() + 1;
                        int charOff = prefixLen;
                        for (int j = 0; j < bi; ++j) {
                            QStringList pp;
                            for (int ci = 0; ci < numCols; ++ci) {
                                if (j < dl.cols.at(ci).size())
                                    pp << dl.cols.at(ci).at(j);
                            }
                            charOff += pp.join(QLatin1Char('/')).length() + 1;
                        }
                        const int cellEnd = charOff + (cellW / m_charW);
                        if (cellEnd <= selHi.column) {
                            p.fillRect(x, yTop, cellW, m_lineH, kSelBg);
                        }
                    }
                }

                int innerX = x;
                for (int ci = 0; ci < numCols; ++ci) {
                    if (bi >= dl.cols.at(ci).size()) {
                        continue;
                    }
                    const QString token = dl.cols.at(ci).at(bi);
                    int formatType = -1;
                    int activeCount = 0;
                    if (m_showHex) {
                        if (activeCount == ci) {
                            formatType = 0;
                        }
                        activeCount++;
                    }
                    if (m_showDec) {
                        if (activeCount == ci) {
                            formatType = 1;
                        }
                        activeCount++;
                    }
                    if (m_showBin) {
                        if (activeCount == ci) {
                            formatType = 2;
                        }
                        activeCount++;
                    }

                    int tokenW = token.length() * m_charW;
                    if (formatType == 0) {
                        // HEX (Clean text)
                        p.setPen(fgColor);
                        p.drawText(innerX, yBase, token);
                        innerX += tokenW;
                    } else {
                        // DEC or BIN (Badged pill)
                        QColor bg = (formatType == 1) ? QColor(76, 175, 80, 20) : QColor(156, 39, 176, 20);
                        QColor border = (formatType == 1) ? QColor(76, 175, 80, 60) : QColor(156, 39, 176, 60);

                        QRect badgeRect(innerX, yTop + 1, tokenW + 4, m_lineH - 2);
                        p.setBrush(bg);
                        p.setPen(border);
                        p.drawRoundedRect(badgeRect, 3, 3);

                        p.setPen(fgColor);
                        p.drawText(innerX + 2, yBase, token);
                        innerX += tokenW + 6;
                    }

                    if (ci < numCols - 1) {
                        innerX += 4;  // gap between columns
                    }
                }

                x += cellW + m_charW;  // advance x to next cell (including inter-cell space)
            }
        }

        if (numCols > 0 && !dl.ascii.isEmpty()) {
            p.setPen(QColor(85, 85, 85));  // Muted separator color
            p.drawText(x, yBase, QStringLiteral("  |  "));
            x += 5 * m_charW;
        }

        if (!dl.ascii.isEmpty()) {
            p.setPen(fgColor);
            p.drawText(x, yBase, dl.ascii);
        }
    }
}

}  // namespace aether
