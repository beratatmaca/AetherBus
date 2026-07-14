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
    const int lastLine = qMin(static_cast<int>(m_lines.size()) - 1, firstLine + viewH / m_lineH + 1);

    int colFormat[3] = {-1, -1, -1};
    int activeCols = 0;
    if (m_showHex) {
        colFormat[activeCols++] = 0;
    }
    if (m_showDec) {
        colFormat[activeCols++] = 1;
    }
    if (m_showBin) {
        colFormat[activeCols++] = 2;
    }

    // Selection span is constant across the paint; compute it once.
    const bool anchorFirst =
        m_selAnchor.line < m_selEnd.line || (m_selAnchor.line == m_selEnd.line && m_selAnchor.column <= m_selEnd.column);
    const CursorPos selLo = anchorFirst ? m_selAnchor : m_selEnd;
    const CursorPos selHi = anchorFirst ? m_selEnd : m_selAnchor;

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
        const int numCols = static_cast<int>(dl.cols.size());
        const int numBytes = static_cast<int>(dl.bytes.size());

        if (numCols > 0) {
            int cellW = 0;
            int cellCharLen = 0;  // '/'-joined token length, matching lineToPlain
            for (int ci = 0; ci < numCols; ++ci) {
                const int tokenLen = static_cast<int>(dl.cols.at(ci).at(0).length());
                cellW += (colFormat[ci] == 0) ? tokenLen * m_charW : tokenLen * m_charW + 6;
                if (ci < numCols - 1) {
                    cellW += 4;
                }
                cellCharLen += tokenLen + (ci > 0 ? 1 : 0);  // '/' between columns
            }
            const int prefixLen = static_cast<int>(dl.prefix.length()) + 1;
            const int cellStride = cellW + m_charW;
            const int charStride = cellCharLen + 1;  // token span plus trailing space
            const int cellCharWidth = cellW / m_charW;

            // Narrow the hit list to this line once (hits are stored in
            // ascending line order) instead of scanning it per byte.
            int hitBegin = 0;
            int hitHi = static_cast<int>(m_searchHits.size());
            while (hitBegin < hitHi) {
                const int mid = (hitBegin + hitHi) / 2;
                if (m_searchHits.at(mid).line < li) {
                    hitBegin = mid + 1;
                } else {
                    hitHi = mid;
                }
            }
            int hitEnd = hitBegin;
            while (hitEnd < m_searchHits.size() && m_searchHits.at(hitEnd).line == li) {
                ++hitEnd;
            }

            for (int bi = 0; bi < numBytes; ++bi) {
                const int charOff = prefixLen + bi * charStride;

                for (int h = hitBegin; h < hitEnd; ++h) {
                    const SearchHit &hit = m_searchHits.at(h);
                    if (hit.start <= charOff && charOff < hit.start + hit.len) {
                        p.fillRect(x, yTop, cellW, m_lineH, kSearchBg);
                        break;
                    }
                }

                if (m_hasSelection) {
                    const bool lineInSel = li > selLo.line && li < selHi.line;
                    const bool lineIsLo = li == selLo.line;
                    const bool lineIsHi = li == selHi.line;
                    const int cellEnd = charOff + cellCharWidth;

                    if (lineInSel) {
                        p.fillRect(x, yTop, cellW + m_charW, m_lineH, kSelBg);
                    } else {
                        bool fill = false;
                        if (lineIsLo && lineIsHi) {
                            fill = (cellEnd > selLo.column && charOff < selHi.column);
                        } else if (lineIsLo) {
                            fill = (charOff >= selLo.column);
                        } else if (lineIsHi) {
                            fill = (cellEnd <= selHi.column);
                        }
                        if (fill) {
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
                    const int formatType = colFormat[ci];
                    const int tokenW = static_cast<int>(token.length()) * m_charW;

                    if (formatType == 0) {
                        // HEX (clean text)
                        p.setPen(fgColor);
                        p.drawText(innerX, yBase, token);
                        innerX += tokenW;
                    } else {
                        // DEC or BIN (badged pill)
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

                x += cellStride;  // advance to the next cell (including inter-cell space)
            }
        }

        if (numCols > 0 && !dl.ascii.isEmpty()) {
            x = qMax(x, m_asciiSepCol - hOff);
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
