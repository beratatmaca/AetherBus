#include "gui/consoleview.h"
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

        // ---------- Prefix ----------
        p.setPen(kMetaFg);
        p.drawText(x, yBase, dl.prefix);
        x += (dl.prefix.length() + 1) * m_charW;

        // ---------- Byte cells ----------
        const QColor &fgColor = isRx ? kRxFg : kTxFg;
        p.setPen(fgColor);

        const int numCols = dl.cols.size();
        const int numBytes = dl.bytes.size();

        for (int bi = 0; bi < numBytes; ++bi) {
            // Build the stacked cell text (col0/col1/… vertically separated by
            // '/' in plain paint — we stack columns in the same cell width).
            // For simplicity we draw one column per line within the cell height.
            // With only 1 column visible it fits in one line; with 2+ columns we
            // compress them side-by-side separated by '/'.
            QStringList cellParts;
            cellParts.reserve(numCols);
            for (int ci = 0; ci < numCols; ++ci) {
                if (bi < dl.cols.at(ci).size()) {
                    cellParts << dl.cols.at(ci).at(bi);
                }
            }
            const QString cellText = cellParts.join(QLatin1Char('/'));
            const int cellW = cellText.length() * m_charW;

            // --- Search-hit overlay ---
            for (const SearchHit &hit : m_searchHits) {
                if (hit.line != li) {
                    continue;
                }
                const int prefixLen = dl.prefix.length() + 1;
                // Compute character offset of this byte cell in the plain text.
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

            // --- Selection overlay ---
            if (m_hasSelection) {
                // Normalise selection so anchor <= end.
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
                    // Single-line selection.
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
                    const int cellEnd = charOff + cellText.length();
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
                    const int cellEnd = charOff + cellText.length();
                    if (cellEnd <= selHi.column) {
                        p.fillRect(x, yTop, cellW, m_lineH, kSelBg);
                    }
                }
            }

            // Draw cell text.
            p.setPen(fgColor);
            p.drawText(x, yBase, cellText);
            x += (cellText.length() + 1) * m_charW;  // +1 for inter-byte space
        }
    }
}

}  // namespace aether
