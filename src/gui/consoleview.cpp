#include "gui/consoleview.h"

#include "core/format_codec.h"

#include <QDateTime>
#include <QFile>
#include <QFontDatabase>
#include <QScrollBar>
#include <QStringList>
#include <QTextCursor>
#include <QTimer>

namespace aether {

namespace {
constexpr int kFlushIntervalMs = 16;  // ~60 Hz
constexpr int kMaxBlocks = 10000;     // rolling history ceiling

// Contrasting colours for the two streams.
const QString kRxColor = QStringLiteral("#4fc3f7");    // peripheral -> host
const QString kTxColor = QStringLiteral("#ffb74d");    // host -> peripheral
const QString kMetaColor = QStringLiteral("#7f8c8d");  // timestamps / separators
}  // namespace

ConsoleView::ConsoleView(QWidget *parent) : QPlainTextEdit(parent) {
    setReadOnly(true);
    setMaximumBlockCount(kMaxBlocks);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &ConsoleView::flush);
    m_flushTimer->start();

    connect(this, &QPlainTextEdit::selectionChanged, this,
            [this] { emit selectionChars(static_cast<int>(textCursor().selectedText().length())); });
}

void ConsoleView::appendChunk(const aether::CapturedChunk &chunk) {
    // Count every byte immediately, even while paused.
    if (chunk.dir == Direction::Rx) {
        m_rx += chunk.data.size();
    } else {
        m_tx += chunk.data.size();
    }
    emit countsChanged(m_rx, m_tx);

    m_pending.append(chunk);
    m_history.append(chunk);
    if (m_history.size() > kMaxBlocks) {
        m_history.removeFirst();
    }
}

void ConsoleView::clearConsole() {
    m_pending.clear();
    m_history.clear();
    m_curBytes.clear();
    m_openRendered = false;
    clear();
}

void ConsoleView::setFormat(Format format) {
    m_format = format;
    reapplyHistory();
}

void ConsoleView::reapplyHistory() {
    // Temporarily disable logging to avoid duplication during replay
    QFile *tempLog = m_logFile;
    m_logFile = nullptr;

    m_curBytes.clear();
    m_openRendered = false;
    clear();

    for (const CapturedChunk &chunk : m_history) {
        processChunk(chunk);
    }

    m_logFile = tempLog;
}

void ConsoleView::setNewlineMode(NewlineMode mode, int param) {
    m_mode = mode;
    m_newlineParam = param;
    reapplyHistory();
}

void ConsoleView::setShowControlChars(bool on) {
    m_showControl = on;
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
    m_rx = 0;
    m_tx = 0;
    emit countsChanged(m_rx, m_tx);
}

QString ConsoleView::buildLineHtml() const {
    const QString ts = QDateTime::fromMSecsSinceEpoch(m_curTs).toString(QStringLiteral("HH:mm:ss.zzz"));
    const bool rx = m_curDir == Direction::Rx;
    const QString tag = rx ? QStringLiteral("Rx") : QStringLiteral("Tx");
    const QString color = rx ? kRxColor : kTxColor;

    // Premium solid background highlights fully supported by Qt
    QString cellStyle;
    if (rx) {
        cellStyle = QStringLiteral("background-color: #162c3d; color: #4fc3f7; font-family: monospace; font-size: 13px; font-weight: bold;");
    } else {
        cellStyle = QStringLiteral("background-color: #2b1d14; color: #ffb74d; font-family: monospace; font-size: 13px; font-weight: bold;");
    }

    QStringList byteCells;
    for (int i = 0; i < m_curBytes.size(); ++i) {
        const auto b = static_cast<unsigned char>(m_curBytes.at(i));
        QString val;
        switch (m_format) {
            case Format::Hex:
                val = QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
                break;
            case Format::Decimal:
                val = QStringLiteral("%1").arg(b, 3, 10, QLatin1Char('0'));
                break;
            case Format::Binary:
                val = QStringLiteral("%1").arg(b, 8, 2, QLatin1Char('0'));
                break;
            case Format::Ascii:
                val = (b >= 0x20 && b < 0x7F) ? QString(QLatin1Char(static_cast<char>(b))) : QStringLiteral(".");
                break;
            case Format::HexAscii: {
                const QString hex = QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
                const QString asc = (b >= 0x20 && b < 0x7F) ? QString(QLatin1Char(static_cast<char>(b))) : QStringLiteral(".");
                val = QStringLiteral("%1:%2").arg(hex, asc);
                break;
            }
        }
        byteCells << QStringLiteral("<span style='%1'>&nbsp;%2&nbsp;</span>").arg(cellStyle, val.toHtmlEscaped());
    }

    QString asciiPart;
    if (m_format != Format::Ascii && m_format != Format::HexAscii) {
        const QString ascii = m_showControl ? codec::toAsciiEscaped(m_curBytes) : codec::toAscii(m_curBytes);
        asciiPart = QStringLiteral("<span style='color:%1; font-family:monospace; margin-left: 10px;'>|  %2</span>").arg(kMetaColor, ascii.toHtmlEscaped());
    }

    return QStringLiteral(
               "<span style='color:%1'>[%2 %3]</span> "
               "%4%5")
        .arg(kMetaColor, ts, tag, byteCells.join(QStringLiteral(" ")), asciiPart);
}

void ConsoleView::renderOpenLine() {
    const QString html = buildLineHtml();
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    if (m_openRendered) {
        // Replace the current (last) block's contents in place.
        cursor.select(QTextCursor::LineUnderCursor);
        cursor.removeSelectedText();
    } else if (!document()->isEmpty()) {
        cursor.insertBlock();
    }
    cursor.insertHtml(html);
    m_openRendered = true;
}

QString ConsoleView::buildLinePlain() const {
    QString out;
    switch (m_format) {
        case Format::Hex:
            out = codec::toHex(m_curBytes);
            break;
        case Format::Decimal:
            out = codec::toDecimal(m_curBytes);
            break;
        case Format::Binary:
            out = codec::toBinary(m_curBytes);
            break;
        case Format::Ascii:
            out = (m_showControl ? codec::toAsciiEscaped(m_curBytes) : codec::toAscii(m_curBytes));
            break;
        case Format::HexAscii: {
            QStringList parts;
            for (int i = 0; i < m_curBytes.size(); ++i) {
                const auto b = static_cast<unsigned char>(m_curBytes.at(i));
                const QString hex = QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
                const QString asc = (b >= 0x20 && b < 0x7F) ? QString(QLatin1Char(static_cast<char>(b))) : QStringLiteral(".");
                parts << QStringLiteral("%1:%2").arg(hex, asc);
            }
            out = parts.join(QStringLiteral(" "));
            break;
        }
    }

    if (m_format != Format::Ascii && m_format != Format::HexAscii) {
        const QString ascii = (m_showControl ? codec::toAsciiEscaped(m_curBytes) : codec::toAscii(m_curBytes));
        out += QStringLiteral("  |  ") + ascii;
    }

    const QString ts = QDateTime::fromMSecsSinceEpoch(m_curTs).toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString tag = m_curDir == Direction::Rx ? QStringLiteral("Rx") : QStringLiteral("Tx");
    return QStringLiteral("[%1 %2] %3").arg(ts, tag, out);
}

void ConsoleView::finalizeLine() {
    // Log the completed line (full session, independent of the display cap).
    if (m_logFile != nullptr && !m_curBytes.isEmpty()) {
        m_logFile->write(buildLinePlain().toUtf8());
        m_logFile->write("\n");
    }
    m_curBytes.clear();
    m_openRendered = false;
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
    if (m_logFile != nullptr) {
        m_logFile->close();
        m_logFile->deleteLater();
        m_logFile = nullptr;
    }
}

void ConsoleView::beginLineIfEmpty(const CapturedChunk &chunk) {
    if (m_curBytes.isEmpty()) {
        m_curDir = chunk.dir;
        m_curTs = chunk.timestampMs;
        m_openRendered = false;
    }
}

void ConsoleView::processChunk(const CapturedChunk &chunk) {
    // A direction change always closes the current line.
    if (!m_curBytes.isEmpty() && chunk.dir != m_curDir) {
        renderOpenLine();
        finalizeLine();
    }

    switch (m_mode) {
        case NewlineMode::PerChunk:
            beginLineIfEmpty(chunk);
            m_curBytes.append(chunk.data);
            renderOpenLine();
            finalizeLine();
            break;

        case NewlineMode::Delimiter:
            for (const char c : chunk.data) {
                beginLineIfEmpty(chunk);
                m_curBytes.append(c);
                if (static_cast<unsigned char>(c) == static_cast<unsigned char>(m_newlineParam)) {
                    renderOpenLine();
                    finalizeLine();
                }
            }
            if (!m_curBytes.isEmpty()) {
                renderOpenLine();  // show the partial, still-open line
            }
            break;

        case NewlineMode::FixedCount: {
            const int n = m_newlineParam > 0 ? m_newlineParam : 16;
            for (const char c : chunk.data) {
                beginLineIfEmpty(chunk);
                m_curBytes.append(c);
                if (m_curBytes.size() >= n) {
                    renderOpenLine();
                    finalizeLine();
                }
            }
            if (!m_curBytes.isEmpty()) {
                renderOpenLine();
            }
            break;
        }
    }
}

void ConsoleView::flush() {
    if (m_paused || m_pending.isEmpty()) {
        return;
    }

    QScrollBar *bar = verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;

    for (const CapturedChunk &chunk : m_pending) {
        processChunk(chunk);
    }
    m_pending.clear();

    if (m_autoScroll && atBottom) {
        bar->setValue(bar->maximum());
    }
}

void ConsoleView::highlightSearchText(const QString &text) {
    QList<QTextEdit::ExtraSelection> selections;
    if (!text.isEmpty()) {
        QTextDocument *doc = document();
        QTextCursor cursor(doc);
        QTextCharFormat format;
        format.setBackground(QColor(255, 235, 59, 120));  // Semi-transparent yellow highlight
        format.setForeground(QColor(0, 0, 0));            // Dark text

        while (!cursor.isNull() && !cursor.atEnd()) {
            cursor = doc->find(text, cursor);
            if (!cursor.isNull()) {
                QTextEdit::ExtraSelection sel;
                sel.format = format;
                sel.cursor = cursor;
                selections.append(sel);
            }
        }
    }
    setExtraSelections(selections);
}

}  // namespace aether
