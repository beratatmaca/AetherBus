// ConsoleView — QAbstractScrollArea + custom paintEvent implementation.
//
// Rendering model
// ===============
// The widget maintains a QVector<DisplayLine> ring buffer of at most kMaxLines
// finalized lines.  paintEvent() iterates only over the lines visible in the
// current scroll position and draws them with QPainter — no HTML DOM, no
// QTextDocument, no per-byte QTextBlock.
//
// Each DisplayLine stores pre-tokenised strings for every active column (HEX,
// DEC, BIN, ASCII) so the paint path never formats bytes — it only measures
// and draws pre-computed strings.
//
// Selection model
// ===============
// Mouse press/move/release track a (line, charOffset) anchor and end.  The
// selected plain text is built on demand from the DisplayLine ring buffer.
//
// Search
// ======
// highlightSearchText() scans every line's plain-text representation and
// records (line, start, len) hit triples.  The search hits are painted as
// yellow overlays inside paintEvent().
//
// Compatibility
// =============
// All public slots, signals, and methods called by mainwindow.cpp are kept
// identical.  The QTextCursor / QTextDocument surface is replaced with a
// lightweight CursorPos struct; the mainwindow doFind() helper is updated to
// use moveCursorToStart/End() instead of QTextCursor moves.

#include "gui/consoleview.h"

#include "core/format_codec.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QFile>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

namespace aether {

namespace {
constexpr int kFlushIntervalMs = 16;       // ~60 Hz
constexpr int kMaxLines = 10000;           // rolling history ceiling
constexpr int kThroughputWindowMs = 1000;  // rate averaging window
constexpr int kLeftPad = 4;                // left margin in pixels

// Contrasting colours for the two streams.
const QColor kRxBg(22, 44, 61);             // dark teal bg
const QColor kTxBg(43, 29, 20);             // dark amber bg
const QColor kRxFg(79, 195, 247);           // bright teal text
const QColor kTxFg(255, 183, 77);           // bright amber text
const QColor kMetaFg(127, 140, 141);        // grey for timestamp prefix
const QColor kSelBg(58, 120, 200, 160);     // selection overlay
const QColor kSearchBg(255, 235, 59, 140);  // search-hit overlay
const QColor kSearchFg(0, 0, 0);

/// QTextDocument::FindBackward flag value (== 1).
constexpr int kFindBackward = 1;
}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ConsoleView::ConsoleView(QWidget *parent) : QAbstractScrollArea(parent) {
    // Fixed-width font — same choice as the old QPlainTextEdit path.
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_font.setPointSize(10);
    setFont(m_font);

    QFontMetrics fm(m_font);
    m_charW = fm.horizontalAdvance(QLatin1Char('M'));
    m_lineH = fm.height() + 2;  // small leading
    m_fontAscent = fm.ascent();

    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    viewport()->setFont(m_font);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setMouseTracking(true);

    // Flush timer — coalesces incoming chunks to ~60 fps.
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &ConsoleView::flush);
    m_flushTimer->start();
}

// ---------------------------------------------------------------------------
// Public API — data ingestion
// ---------------------------------------------------------------------------

void ConsoleView::appendChunk(const aether::CapturedChunk &chunk) {
    const int n = chunk.data.size();
    if (chunk.dir == Direction::Rx) {
        m_rx += n;
        m_rxWindow += n;
    } else {
        m_tx += n;
        m_txWindow += n;
    }

    if (!m_rateTimer.isValid()) {
        m_rateTimer.start();
    }
    const qint64 elapsed = m_rateTimer.elapsed();
    if (elapsed >= kThroughputWindowMs) {
        m_rxRate = static_cast<qint64>(m_rxWindow * 1000LL / elapsed);
        m_txRate = static_cast<qint64>(m_txWindow * 1000LL / elapsed);
        m_rxWindow = 0;
        m_txWindow = 0;
        m_rateTimer.restart();
    }

    emit countsChanged(m_rx, m_tx, m_rxRate, m_txRate);

    m_pending.append(chunk);
    m_history.append(chunk);
    if (m_history.size() > kMaxLines) {
        m_history.removeFirst();
    }
}

void ConsoleView::clearConsole() {
    m_pending.clear();
    m_history.clear();
    m_lines.clear();
    m_openLineActive = false;
    m_openLine = DisplayLine{};
    m_hasSelection = false;
    m_searchHits.clear();
    m_cursor = CursorPos{};
    updateScrollBars();
    viewport()->update();
}

// ---------------------------------------------------------------------------
// Public API — display settings
// ---------------------------------------------------------------------------

void ConsoleView::setFormats(bool hex, bool dec, bool bin, bool ascii) {
    m_showHex = hex;
    m_showDec = dec;
    m_showBin = bin;
    m_showAscii = (!hex && !dec && !bin) ? true : ascii;
    reapplyHistory();
}

void ConsoleView::setTlvParams(int headerSize, int lenOffset, int lenSize) {
    m_tlvHeaderSize = qMax(1, headerSize);
    m_tlvLenOffset = qBound(0, lenOffset, m_tlvHeaderSize - 1);
    m_tlvLenSize = (lenSize >= 2) ? 2 : 1;
    m_tlvTargetSize = -1;
    reapplyHistory();
}

void ConsoleView::setNewlineMode(NewlineMode mode, int param) {
    m_mode = mode;
    m_newlineParam = param;
    reapplyHistory();
}

void ConsoleView::setShowTimestamps(bool on) {
    m_showTimestamps = on;
    reapplyHistory();
}

void ConsoleView::setAutoScroll(bool on) {
    m_autoScroll = on;
    if (on) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
}

void ConsoleView::setPaused(bool paused) {
    m_paused = paused;
}

void ConsoleView::resetCounts() {
    m_rx = m_tx = 0;
    m_rxWindow = m_txWindow = 0;
    m_rxRate = m_txRate = 0;
    m_rateTimer.invalidate();
    emit countsChanged(m_rx, m_tx, m_rxRate, m_txRate);
}

bool ConsoleView::startLogging(const QString &path) {
    stopLogging();
    auto *file = new QFile(path, this);
    if (!file->open(QFile::WriteOnly | QFile::Append | QFile::Text)) {
        file->deleteLater();
        return false;
    }
    m_logFile = file;
    return true;
}

void ConsoleView::stopLogging() {
    if (m_logFile) {
        m_logFile->close();
        m_logFile->deleteLater();
        m_logFile = nullptr;
    }
}

// ---------------------------------------------------------------------------
// toPlainText — used by MainWindow::saveReceived()
// ---------------------------------------------------------------------------

QString ConsoleView::toPlainText() const {
    QStringList out;
    out.reserve(m_lines.size());
    for (const DisplayLine &dl : m_lines) {
        out << lineToPlain(dl);
    }
    return out.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------------------
// Cursor / find surface (replaces QPlainTextEdit cursor API)
// ---------------------------------------------------------------------------

void ConsoleView::setTextCursor(const CursorPos &c) {
    m_cursor = c;
}

void ConsoleView::moveCursorToStart() {
    m_cursor = CursorPos{0, 0};
}

void ConsoleView::moveCursorToEnd() {
    m_cursor = CursorPos{qMax(0, m_lines.size() - 1), 0};
}

bool ConsoleView::findQuery(const QString &query, int flags) {
    if (query.isEmpty() || m_lines.isEmpty()) {
        return false;
    }

    const bool backward = (flags & kFindBackward) != 0;
    const QRegularExpression regex = buildSearchRegex(query);
    const bool useRegex = regex.isValid() && !regex.pattern().isEmpty();

    const int total = m_lines.size();
    int startLine = qBound(0, m_cursor.line, total - 1);

    // Search from current cursor position.
    for (int i = 0; i < total; ++i) {
        int li = backward ? ((startLine - i - 1 + total) % total) : ((startLine + i + 1) % total);

        const QString text = lineSearchText(m_lines.at(li));
        int matchStart = -1;
        int matchLen = 0;

        if (useRegex) {
            const QRegularExpressionMatch m = regex.match(text);
            if (m.hasMatch()) {
                matchStart = m.capturedStart();
                matchLen = m.capturedLength();
            }
        } else {
            matchStart = text.indexOf(query, 0, Qt::CaseInsensitive);
            matchLen = query.length();
        }

        if (matchStart >= 0) {
            m_cursor = CursorPos{li, matchStart};
            // Scroll to make the found line visible.
            QScrollBar *vsb = verticalScrollBar();
            vsb->setValue(qBound(vsb->minimum(), li * m_lineH - viewport()->height() / 2, vsb->maximum()));
            // Highlight as selection.
            m_selAnchor = CursorPos{li, matchStart};
            m_selEnd = CursorPos{li, matchStart + matchLen};
            m_hasSelection = true;
            emit selectionChars(matchLen);
            viewport()->update();
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Search highlight
// ---------------------------------------------------------------------------

void ConsoleView::highlightSearchText(const QString &text) {
    m_searchText = text;
    m_searchHits.clear();

    if (!text.isEmpty()) {
        const QRegularExpression regex = buildSearchRegex(text);
        const bool useRegex = regex.isValid() && !regex.pattern().isEmpty();

        for (int li = 0; li < m_lines.size(); ++li) {
            const QString lineText = lineSearchText(m_lines.at(li));
            if (useRegex) {
                QRegularExpressionMatchIterator it = regex.globalMatch(lineText);
                while (it.hasNext()) {
                    const QRegularExpressionMatch m = it.next();
                    m_searchHits.append(SearchHit{li, m.capturedStart(), m.capturedLength()});
                }
            } else {
                int pos = 0;
                while ((pos = lineText.indexOf(text, pos, Qt::CaseInsensitive)) >= 0) {
                    m_searchHits.append(SearchHit{li, pos, text.length()});
                    pos += text.length();
                }
            }
        }
    }
    viewport()->update();
}

// ---------------------------------------------------------------------------
// Flush — called by timer, drains m_pending into m_lines
// ---------------------------------------------------------------------------

void ConsoleView::flush() {
    if (m_paused || m_pending.isEmpty()) {
        return;
    }

    QScrollBar *vsb = verticalScrollBar();
    const bool atBottom = vsb->value() >= vsb->maximum() - m_lineH;

    for (const CapturedChunk &chunk : m_pending) {
        processChunk(chunk);
    }
    m_pending.clear();

    updateScrollBars();

    if (m_autoScroll && atBottom) {
        vsb->setValue(vsb->maximum());
    }

    viewport()->update();
}

// ---------------------------------------------------------------------------
// Chunk processing — identical logic to the old code but writes to m_lines
// ---------------------------------------------------------------------------

void ConsoleView::beginLineIfEmpty(const CapturedChunk &chunk) {
    if (m_openLine.bytes.isEmpty()) {
        m_openLine.dir = chunk.dir;
        m_openLine.timestampMs = chunk.timestampMs;
    }
}

void ConsoleView::renderOpenLine() {
    // Re-render the open line in-place (it is always the last element).
    m_openLine = buildLine(m_openLine.dir, m_openLine.timestampMs, m_openLine.bytes);
    if (m_openLineActive) {
        // Overwrite the last element with the freshly rendered version.
        m_lines.last() = m_openLine;
    } else {
        m_lines.append(m_openLine);
        m_openLineActive = true;
        // Enforce ring-buffer ceiling.
        if (m_lines.size() > kMaxLines) {
            m_lines.removeFirst();
            // Shift search hits and cursor down by one.
            for (auto &h : m_searchHits) {
                --h.line;
            }
            if (m_cursor.line > 0) {
                --m_cursor.line;
            }
        }
    }
}

void ConsoleView::finalizeLine() {
    // Log the completed line.
    if (m_logFile && !m_openLine.bytes.isEmpty()) {
        m_logFile->write(lineToPlain(m_openLine).toUtf8());
        m_logFile->write("\n");
    }
    m_openLine = DisplayLine{};
    m_openLineActive = false;
    m_tlvTargetSize = -1;
}

void ConsoleView::commitOpenLine() {
    // Ensures the open line is in m_lines, then resets state.
    if (!m_openLineActive && !m_openLine.bytes.isEmpty()) {
        renderOpenLine();
    }
    finalizeLine();
}

void ConsoleView::processChunk(const CapturedChunk &chunk) {
    // Direction change always closes the current line.
    if (!m_openLine.bytes.isEmpty() && chunk.dir != m_openLine.dir) {
        renderOpenLine();
        finalizeLine();
    }

    switch (m_mode) {
        case NewlineMode::PerChunk:
            beginLineIfEmpty(chunk);
            m_openLine.bytes.append(chunk.data);
            renderOpenLine();
            finalizeLine();
            break;

        case NewlineMode::Delimiter:
            for (const char c : chunk.data) {
                beginLineIfEmpty(chunk);
                m_openLine.bytes.append(c);
                if (static_cast<unsigned char>(c) == static_cast<unsigned char>(m_newlineParam)) {
                    renderOpenLine();
                    finalizeLine();
                }
            }
            if (!m_openLine.bytes.isEmpty()) {
                renderOpenLine();
            }
            break;

        case NewlineMode::FixedCount: {
            const int n = m_newlineParam > 0 ? m_newlineParam : 16;
            for (const char c : chunk.data) {
                beginLineIfEmpty(chunk);
                m_openLine.bytes.append(c);
                if (m_openLine.bytes.size() >= n) {
                    renderOpenLine();
                    finalizeLine();
                }
            }
            if (!m_openLine.bytes.isEmpty()) {
                renderOpenLine();
            }
            break;
        }

        case NewlineMode::TLV: {
            for (const char c : chunk.data) {
                beginLineIfEmpty(chunk);
                m_openLine.bytes.append(c);

                if (m_tlvTargetSize == -1 && m_openLine.bytes.size() >= m_tlvHeaderSize) {
                    const auto *hdr = reinterpret_cast<const unsigned char *>(m_openLine.bytes.constData());
                    int payloadLen = 0;
                    if (m_tlvLenSize >= 2) {
                        payloadLen = (static_cast<int>(hdr[m_tlvLenOffset]) << 8) | static_cast<int>(hdr[m_tlvLenOffset + 1]);
                    } else {
                        payloadLen = static_cast<int>(hdr[m_tlvLenOffset]);
                    }
                    m_tlvTargetSize = m_tlvHeaderSize + payloadLen;
                }

                if (m_tlvTargetSize > 0 && m_openLine.bytes.size() >= m_tlvTargetSize) {
                    renderOpenLine();
                    finalizeLine();
                }
            }
            if (!m_openLine.bytes.isEmpty()) {
                renderOpenLine();
            }
            break;
        }

        case NewlineMode::CrLf:
            for (const char c : chunk.data) {
                if (c == '\n' && m_pendingCr) {
                    m_pendingCr = false;
                    continue;
                }
                m_pendingCr = (c == '\r');
                if (c == '\r' || c == '\n') {
                    if (!m_openLine.bytes.isEmpty()) {
                        renderOpenLine();
                        finalizeLine();
                    }
                } else {
                    beginLineIfEmpty(chunk);
                    m_openLine.bytes.append(c);
                }
            }
            if (!m_openLine.bytes.isEmpty()) {
                renderOpenLine();
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// DisplayLine building
// ---------------------------------------------------------------------------

DisplayLine ConsoleView::buildLine(Direction dir, qint64 tsMs, const QByteArray &bytes) const {
    DisplayLine dl;
    dl.dir = dir;
    dl.timestampMs = tsMs;
    dl.bytes = bytes;

    // Build prefix.
    const QString tag = (dir == Direction::Rx) ? QStringLiteral("Rx") : QStringLiteral("Tx");
    if (m_showTimestamps) {
        const QString ts = QDateTime::fromMSecsSinceEpoch(tsMs).toString(QStringLiteral("HH:mm:ss.zzz"));
        dl.prefix = QStringLiteral("[%1 %2]").arg(ts, tag);
    } else {
        dl.prefix = QStringLiteral("[%1]").arg(tag);
    }

    // Build per-column token arrays.
    // Column order: HEX, DEC, BIN, ASCII (matching the old HTML build order).
    dl.cols.clear();

    auto makeCol = [&](auto formatter) {
        QStringList tokens;
        tokens.reserve(bytes.size());
        for (int i = 0; i < bytes.size(); ++i) {
            tokens << formatter(static_cast<unsigned char>(bytes.at(i)));
        }
        dl.cols.append(tokens);
    };

    if (m_showHex) {
        makeCol([](unsigned char b) { return QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper(); });
    }
    if (m_showDec) {
        makeCol([](unsigned char b) { return QStringLiteral("%1").arg(b, 3, 10, QLatin1Char('0')); });
    }
    if (m_showBin) {
        makeCol([](unsigned char b) { return QStringLiteral("%1").arg(b, 8, 2, QLatin1Char('0')); });
    }
    if (m_showAscii) {
        makeCol([](unsigned char b) -> QString {
            return (b >= 0x20 && b < 0x7F) ? QString(QLatin1Char(static_cast<char>(b))) : QStringLiteral(".");
        });
    }

    return dl;
}

// ---------------------------------------------------------------------------
// Plain-text rendering (for logging + toPlainText())
// ---------------------------------------------------------------------------

QString ConsoleView::lineToPlain(const DisplayLine &dl) const {
    // Build "col0/col1/col2…" per byte, joined by spaces.
    QStringList tokens;
    tokens.reserve(dl.bytes.size());
    const int numCols = dl.cols.size();
    for (int bi = 0; bi < dl.bytes.size(); ++bi) {
        QStringList cellParts;
        cellParts.reserve(numCols);
        for (int ci = 0; ci < numCols; ++ci) {
            if (bi < dl.cols.at(ci).size()) {
                cellParts << dl.cols.at(ci).at(bi);
            }
        }
        tokens << cellParts.join(QLatin1Char('/'));
    }
    return QStringLiteral("%1 %2").arg(dl.prefix, tokens.join(QLatin1Char(' ')));
}

QString ConsoleView::lineSearchText(const DisplayLine &dl) const {
    return lineToPlain(dl);
}

// ---------------------------------------------------------------------------
// History reapplication
// ---------------------------------------------------------------------------

void ConsoleView::reapplyHistory() {
    QFile *savedLog = m_logFile;
    m_logFile = nullptr;

    m_lines.clear();
    m_openLine = DisplayLine{};
    m_openLineActive = false;
    m_tlvTargetSize = -1;
    m_pendingCr = false;

    for (const CapturedChunk &chunk : m_history) {
        processChunk(chunk);
    }

    m_logFile = savedLog;

    // Re-run the current search against the new render.
    if (!m_searchText.isEmpty()) {
        highlightSearchText(m_searchText);
    }

    updateScrollBars();
    viewport()->update();
}

// ---------------------------------------------------------------------------
// Scroll bar management
// ---------------------------------------------------------------------------

void ConsoleView::updateScrollBars() {
    const int totalH = m_lines.size() * m_lineH;
    const int viewH = viewport()->height();

    QScrollBar *vsb = verticalScrollBar();
    vsb->setRange(0, qMax(0, totalH - viewH));
    vsb->setPageStep(viewH);
    vsb->setSingleStep(m_lineH);

    // Horizontal scroll: estimate the widest line.
    // For performance we cap the scan.
    int maxW = 0;
    const int scanLimit = qMin(m_lines.size(), 200);
    for (int i = m_lines.size() - scanLimit; i < m_lines.size(); ++i) {
        const DisplayLine &dl = m_lines.at(i);
        // prefix + space + (numCols * tokenWidth + spaces)
        int lineW = kLeftPad + (dl.prefix.length() + 1) * m_charW;
        if (!dl.cols.isEmpty() && !dl.bytes.isEmpty()) {
            int cellW = 0;
            for (const QStringList &col : dl.cols) {
                if (!col.isEmpty()) {
                    cellW += col.first().length();
                }
            }
            // numCols separators (/)  + 1 space between bytes
            lineW += dl.bytes.size() * (cellW * m_charW + (dl.cols.size() - 1) * m_charW + m_charW);
        }
        maxW = qMax(maxW, lineW);
    }

    QScrollBar *hsb = horizontalScrollBar();
    hsb->setRange(0, qMax(0, maxW - viewport()->width()));
    hsb->setPageStep(viewport()->width());
    hsb->setSingleStep(m_charW);
}

int ConsoleView::lineHeight() const {
    return m_lineH;
}

int ConsoleView::firstVisibleLine() const {
    return verticalScrollBar()->value() / m_lineH;
}

int ConsoleView::visibleLineCount() const {
    return viewport()->height() / m_lineH + 2;
}

// ---------------------------------------------------------------------------
// paintEvent — the hot path
// ---------------------------------------------------------------------------

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
                // Approximate: the full plain text of the line is prefix+space+bytes.
                // Hit positions are character offsets in that string.
                // Here we cheaply map byte-cell index to character offset.
                // TODO: precise per-character mapping; this approximation covers
                // full-cell hits which is the common case.
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

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void ConsoleView::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

// ---------------------------------------------------------------------------
// Mouse — selection
// ---------------------------------------------------------------------------

CursorPos ConsoleView::posFromPoint(QPoint pt) const {
    const int vOff = verticalScrollBar()->value();
    const int hOff = horizontalScrollBar()->value();
    const int li = qBound(0, (pt.y() + vOff) / m_lineH, m_lines.size() - 1);

    if (m_lines.isEmpty()) {
        return CursorPos{0, 0};
    }

    // Compute character offset within the line text.
    const int xInLine = pt.x() + hOff - kLeftPad;
    const int col = qMax(0, xInLine / m_charW);
    return CursorPos{li, col};
}

void ConsoleView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_selAnchor = posFromPoint(event->pos());
        m_selEnd = m_selAnchor;
        m_hasSelection = false;
        m_mouseSelecting = true;
        emit selectionChars(0);
        viewport()->update();
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void ConsoleView::mouseMoveEvent(QMouseEvent *event) {
    if (m_mouseSelecting && (event->buttons() & Qt::LeftButton)) {
        m_selEnd = posFromPoint(event->pos());
        m_hasSelection = (m_selAnchor.line != m_selEnd.line || m_selAnchor.column != m_selEnd.column);
        if (m_hasSelection) {
            emit selectionChars(static_cast<int>(selectedText().length()));
        }
        viewport()->update();
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void ConsoleView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_mouseSelecting = false;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

void ConsoleView::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Copy)) {
        // Copy primary layer (layer 0).
        copySelectionToClipboard(0);
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// Wheel
// ---------------------------------------------------------------------------

void ConsoleView::wheelEvent(QWheelEvent *event) {
    QScrollBar *vsb = verticalScrollBar();
    vsb->setValue(vsb->value() - event->angleDelta().y() / 120 * m_lineH * 3);
    event->accept();
}

// ---------------------------------------------------------------------------
// Context menu — "Copy selection as…"
// ---------------------------------------------------------------------------

void ConsoleView::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);

    // Standard actions.
    QAction *actSelectAll = menu.addAction(QStringLiteral("Select all"));
    connect(actSelectAll, &QAction::triggered, this, [this] {
        if (!m_lines.isEmpty()) {
            m_selAnchor = CursorPos{0, 0};
            m_selEnd = CursorPos{m_lines.size() - 1, static_cast<int>(fullLineText(m_lines.size() - 1).length())};
            m_hasSelection = true;
            emit selectionChars(static_cast<int>(selectedText().length()));
            viewport()->update();
        }
    });

    if (m_hasSelection) {
        menu.addSeparator();
        QMenu *copyAs = menu.addMenu(QStringLiteral("Copy selection as…"));

        struct LayerInfo {
            QString label;
            int index;
            bool active;
        };
        const QList<LayerInfo> layers = {
            {QStringLiteral("HEX"), 0, m_showHex},
            {QStringLiteral("DEC"), 1, m_showDec},
            {QStringLiteral("BIN"), 2, m_showBin},
            {QStringLiteral("ASCII"), 3, m_showAscii},
        };

        int slot = 0;
        for (const LayerInfo &info : layers) {
            if (!info.active) {
                continue;
            }
            const int capturedSlot = slot;
            QAction *act = copyAs->addAction(info.label);
            connect(act, &QAction::triggered, this, [this, capturedSlot] { copySelectionToClipboard(capturedSlot); });
            ++slot;
        }

        copyAs->addSeparator();
        QAction *rawHex = copyAs->addAction(QStringLiteral("Raw bytes (HEX, no layers)"));
        connect(rawHex, &QAction::triggered, this, [this] {
            // Walk selected lines and emit the first-column (HEX) tokens only.
            copySelectionToClipboard(0);
        });
    }

    menu.exec(event->globalPos());
}

// ---------------------------------------------------------------------------
// Clipboard helpers
// ---------------------------------------------------------------------------

QString ConsoleView::selectedText() const {
    if (!m_hasSelection || m_lines.isEmpty()) {
        return {};
    }

    // Normalise.
    const CursorPos selLo =
        (m_selAnchor.line < m_selEnd.line || (m_selAnchor.line == m_selEnd.line && m_selAnchor.column <= m_selEnd.column)) ? m_selAnchor
                                                                                                                           : m_selEnd;
    const CursorPos selHi =
        (m_selAnchor.line < m_selEnd.line || (m_selAnchor.line == m_selEnd.line && m_selAnchor.column <= m_selEnd.column)) ? m_selEnd
                                                                                                                           : m_selAnchor;

    QStringList out;
    for (int li = selLo.line; li <= qMin(selHi.line, m_lines.size() - 1); ++li) {
        const QString text = lineToPlain(m_lines.at(li));
        if (li == selLo.line && li == selHi.line) {
            const int s = qBound(0, selLo.column, text.length());
            const int e = qBound(s, selHi.column, text.length());
            out << text.mid(s, e - s);
        } else if (li == selLo.line) {
            out << text.mid(qBound(0, selLo.column, text.length()));
        } else if (li == selHi.line) {
            out << text.left(qBound(0, selHi.column, text.length()));
        } else {
            out << text;
        }
    }
    return out.join(QLatin1Char('\n'));
}

void ConsoleView::copySelectionToClipboard(int layerIndex) const {
    if (!m_hasSelection || m_lines.isEmpty()) {
        return;
    }

    const CursorPos selLo =
        (m_selAnchor.line < m_selEnd.line || (m_selAnchor.line == m_selEnd.line && m_selAnchor.column <= m_selEnd.column)) ? m_selAnchor
                                                                                                                           : m_selEnd;
    const CursorPos selHi =
        (m_selAnchor.line < m_selEnd.line || (m_selAnchor.line == m_selEnd.line && m_selAnchor.column <= m_selEnd.column)) ? m_selEnd
                                                                                                                           : m_selAnchor;

    QStringList out;
    for (int li = selLo.line; li <= qMin(selHi.line, m_lines.size() - 1); ++li) {
        const DisplayLine &dl = m_lines.at(li);
        // Collect tokens from the requested layer column.
        if (layerIndex < dl.cols.size()) {
            out << dl.cols.at(layerIndex).join(QLatin1Char(' '));
        }
    }
    QApplication::clipboard()->setText(out.join(QLatin1Char('\n')));
}

QString ConsoleView::fullLineText(int li) const {
    if (li < 0 || li >= m_lines.size()) {
        return {};
    }
    return lineToPlain(m_lines.at(li));
}

// ---------------------------------------------------------------------------
// Search regex — identical to the old code
// ---------------------------------------------------------------------------

QRegularExpression ConsoleView::buildSearchRegex(const QString &query) const {
    QByteArray bytes;
    QString cleanQuery = query.trimmed();

    QRegularExpression hexPattern(QStringLiteral("^(?:(?:0x)?[0-9a-fA-F]{2}\\s*)+$"));
    if (hexPattern.match(cleanQuery).hasMatch()) {
        QString hexStr = cleanQuery;
        hexStr.remove(QStringLiteral("0x"));
        hexStr.remove(QStringLiteral(" "));
        bytes = QByteArray::fromHex(hexStr.toUtf8());
    } else {
        bytes = query.toUtf8();
    }

    if (bytes.isEmpty()) {
        return QRegularExpression();
    }

    QStringList cellPatterns;
    for (int i = 0; i < bytes.size(); ++i) {
        const auto b = static_cast<unsigned char>(bytes.at(i));
        QStringList layers;
        if (m_showHex) {
            layers << QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
        }
        if (m_showDec) {
            layers << QStringLiteral("%1").arg(b, 3, 10, QLatin1Char('0'));
        }
        if (m_showBin) {
            layers << QStringLiteral("%1").arg(b, 8, 2, QLatin1Char('0'));
        }
        if (m_showAscii) {
            QString asc = (b >= 0x20 && b < 0x7F) ? QString(QLatin1Char(static_cast<char>(b))) : QStringLiteral(".");
            layers << QRegularExpression::escape(asc);
        }

        // Join layers with '/' (the separator used in lineToPlain).
        cellPatterns << QStringLiteral("\\b%1\\b").arg(layers.join(QLatin1Char('/')));
    }

    return QRegularExpression(cellPatterns.join(QStringLiteral("\\s+")), QRegularExpression::CaseInsensitiveOption);
}

}  // namespace aether
