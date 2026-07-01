#include "gui/consoleview.h"

#include <QDateTime>
#include <QFile>
#include <QFontDatabase>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

namespace aether {

namespace {
constexpr int kFlushIntervalMs = 16;       // ~60 Hz
constexpr int kMaxLines = 10000;           // rolling history ceiling
constexpr int kThroughputWindowMs = 1000;  // rate averaging window
}  // namespace

ConsoleView::ConsoleView(QWidget *parent) : QAbstractScrollArea(parent) {
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

    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &ConsoleView::flush);
    m_flushTimer->start();
}

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

QString ConsoleView::toPlainText() const {
    QStringList out;
    out.reserve(m_lines.size());
    for (const DisplayLine &dl : m_lines) {
        out << lineToPlain(dl);
    }
    return out.join(QLatin1Char('\n'));
}

void ConsoleView::setTextCursor(const CursorPos &c) {
    m_cursor = c;
}

void ConsoleView::moveCursorToStart() {
    m_cursor = CursorPos{0, 0};
}

void ConsoleView::moveCursorToEnd() {
    m_cursor = CursorPos{static_cast<int>(qMax(0, m_lines.size() - 1)), 0};
}

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

void ConsoleView::beginLineIfEmpty(const CapturedChunk &chunk) {
    if (m_openLine.bytes.isEmpty()) {
        m_openLine.dir = chunk.dir;
        m_openLine.timestampMs = chunk.timestampMs;
        m_openLine.isFrame = chunk.isFrame;
        m_openLine.frameId = chunk.frameId;
        m_openLine.frameFlags = chunk.frameFlags;
    }
}

void ConsoleView::renderOpenLine() {
    m_openLine =
        buildLine(m_openLine.dir, m_openLine.timestampMs, m_openLine.bytes, m_openLine.isFrame, m_openLine.frameId, m_openLine.frameFlags);
    if (m_openLineActive) {
        m_lines.last() = m_openLine;
    } else {
        m_lines.append(m_openLine);
        m_openLineActive = true;
        if (m_lines.size() > kMaxLines) {
            m_lines.removeFirst();
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
    if (m_logFile && !m_openLine.bytes.isEmpty()) {
        m_logFile->write(lineToPlain(m_openLine).toUtf8());
        m_logFile->write("\n");
    }
    m_openLine = DisplayLine{};
    m_openLineActive = false;
    m_tlvTargetSize = -1;
}

void ConsoleView::commitOpenLine() {
    if (!m_openLineActive && !m_openLine.bytes.isEmpty()) {
        renderOpenLine();
    }
    finalizeLine();
}

void ConsoleView::processChunk(const CapturedChunk &chunk) {
    if (!m_openLine.bytes.isEmpty() && chunk.dir != m_openLine.dir) {
        renderOpenLine();
        finalizeLine();
    }

    switch (m_mode) {
        case NewlineMode::Frame:  // CAN: one atomic line per frame, with a frame header
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

QString ConsoleView::frameHeader(quint32 id, quint16 flags, int payloadLen) {
    const int width = (flags & FrameExtendedId) != 0 ? 8 : 3;
    QString header = QStringLiteral("%1 [%2]").arg(id, width, 16, QLatin1Char('0')).toUpper().arg(payloadLen);
    if ((flags & FrameRemote) != 0) {
        header += QStringLiteral(" R");
    }
    if ((flags & FrameError) != 0) {
        header += QStringLiteral(" ERR");
    }
    if ((flags & FrameFd) != 0) {
        header += (flags & FrameBitRateSwitch) != 0 ? QStringLiteral(" FD/BRS") : QStringLiteral(" FD");
    }
    return header;
}

DisplayLine ConsoleView::buildLine(Direction dir, qint64 tsMs, const QByteArray &bytes, bool isFrame, quint32 frameId,
                                   quint16 frameFlags) const {
    DisplayLine dl;
    dl.dir = dir;
    dl.timestampMs = tsMs;
    dl.bytes = bytes;
    dl.isFrame = isFrame;
    dl.frameId = frameId;
    dl.frameFlags = frameFlags;

    const QString tag = (dir == Direction::Rx) ? QStringLiteral("Rx") : QStringLiteral("Tx");
    if (m_showTimestamps) {
        const QString ts = QDateTime::fromMSecsSinceEpoch(tsMs).toString(QStringLiteral("HH:mm:ss.zzz"));
        dl.prefix = QStringLiteral("[%1 %2]").arg(ts, tag);
    } else {
        dl.prefix = QStringLiteral("[%1]").arg(tag);
    }
    if (isFrame) {
        dl.prefix += QLatin1Char(' ') + frameHeader(frameId, frameFlags, static_cast<int>(bytes.size()));
    }

    dl.cols.clear();

    auto makeCol = [&](auto formatter) {
        QStringList tokens;
        tokens.reserve(bytes.size());
        for (char byte : bytes) {
            tokens << formatter(static_cast<unsigned char>(byte));
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
        dl.ascii.reserve(bytes.size());
        for (char byte : bytes) {
            auto b = static_cast<unsigned char>(byte);
            dl.ascii.append((b >= 0x20 && b < 0x7F) ? QLatin1Char(static_cast<char>(b)) : QLatin1Char('.'));
        }
    }

    return dl;
}

QString ConsoleView::lineToPlain(const DisplayLine &dl) const {
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

    QString numText = tokens.join(QLatin1Char(' '));
    if (!numText.isEmpty() && !dl.ascii.isEmpty()) {
        return QStringLiteral("%1 %2  |  %3").arg(dl.prefix, numText, dl.ascii);
    }
    if (!numText.isEmpty()) {
        return QStringLiteral("%1 %2").arg(dl.prefix, numText);
    }
    return QStringLiteral("%1 %2").arg(dl.prefix, dl.ascii);
}

QString ConsoleView::lineSearchText(const DisplayLine &dl) const {
    return lineToPlain(dl);
}

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

    if (!m_searchText.isEmpty()) {
        highlightSearchText(m_searchText);
    }

    updateScrollBars();
    viewport()->update();
}

void ConsoleView::updateScrollBars() {
    const int totalH = m_lines.size() * m_lineH;
    const int viewH = viewport()->height();

    QScrollBar *vsb = verticalScrollBar();
    vsb->setRange(0, qMax(0, totalH - viewH));
    vsb->setPageStep(viewH);
    vsb->setSingleStep(m_lineH);

    int maxW = 0;
    const int scanLimit = qMin(m_lines.size(), 200);
    for (int i = m_lines.size() - scanLimit; i < m_lines.size(); ++i) {
        const DisplayLine &dl = m_lines.at(i);
        int lineW = kLeftPad + ((dl.prefix.length() + 1) * m_charW);

        int numCols = dl.cols.size();
        if (numCols > 0 && !dl.bytes.isEmpty()) {
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

                int tokenLen = dl.cols.at(ci).first().length();
                if (formatType == 0) {
                    cellW += tokenLen * m_charW;
                } else {
                    cellW += (tokenLen * m_charW) + 6;
                }
                if (ci < numCols - 1) {
                    cellW += 4;
                }
            }
            lineW += dl.bytes.size() * (cellW + m_charW);
        }

        if (numCols > 0 && !dl.ascii.isEmpty()) {
            lineW += 5 * m_charW;  // "  |  "
        }
        if (!dl.ascii.isEmpty()) {
            lineW += dl.ascii.length() * m_charW;
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
    return (viewport()->height() / m_lineH) + 2;
}

void ConsoleView::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void ConsoleView::wheelEvent(QWheelEvent *event) {
    QScrollBar *vsb = verticalScrollBar();
    vsb->setValue(vsb->value() - (event->angleDelta().y() / 120 * m_lineH * 3));
    event->accept();
}

}  // namespace aether
