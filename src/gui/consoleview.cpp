#include "gui/consoleview.h"

#include "core/format_codec.h"

#include <QDateTime>
#include <QFile>
#include <QFontDatabase>
#include <QScrollBar>
#include <QStringList>
#include <QTextCursor>
#include <QTimer>
#include <QMimeData>

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

void ConsoleView::setFormats(bool hex, bool dec, bool bin, bool ascii) {
    m_showHex = hex;
    m_showDec = dec;
    m_showBin = bin;
    // Always keep at least one column visible so lines never render empty.
    m_showAscii = (!hex && !dec && !bin) ? true : ascii;
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

void ConsoleView::setTlvParams(int headerSize, int lenOffset, int lenSize) {
    m_tlvHeaderSize = qMax(1, headerSize);
    m_tlvLenOffset  = qBound(0, lenOffset, m_tlvHeaderSize - 1);
    m_tlvLenSize    = (lenSize >= 2) ? 2 : 1;
    m_tlvTargetSize = -1;
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
        QStringList cellLines;
        if (m_showHex) {
            cellLines << QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
        }
        if (m_showDec) {
            cellLines << QStringLiteral("%1").arg(b, 3, 10, QLatin1Char('0'));
        }
        if (m_showBin) {
            cellLines << QStringLiteral("%1").arg(b, 8, 2, QLatin1Char('0'));
        }
        if (m_showAscii) {
            cellLines << ((b >= 0x20 && b < 0x7F) ? QString(QLatin1Char(static_cast<char>(b))) : QStringLiteral("."));
        }
        
        QStringList escapedLines;
        for (const QString &line : cellLines) {
            escapedLines << line.toHtmlEscaped();
        }
        // U+2028 LINE SEPARATOR = soft line-break within a single paragraph block.
        // Unlike <br> (which creates a new QTextBlock/paragraph), U+2028 keeps
        // all format layers inside one block so the cell background styling is
        // preserved across layers and renderOpenLine anchor tracking stays clean.
        const QString cellContent = escapedLines.join(QStringLiteral("\u2028"));
        byteCells << QStringLiteral("<span style='%1'>&nbsp;%2&nbsp;</span>").arg(cellStyle, cellContent);
    }

    return QStringLiteral(
               "<span style='color:%1'>[%2 %3]</span> "
               "%4")
        .arg(kMetaColor, ts, tag, byteCells.join(QStringLiteral(" ")));
}

void ConsoleView::renderOpenLine() {
    const QString html = buildLineHtml();
    QTextCursor cursor(document());

    if (m_openRendered && m_lineAnchorPos >= 0) {
        // Select from the recorded anchor to the very end of the document
        // and replace it all.  This correctly handles HTML that inserted
        // multiple blocks (e.g. <br> inside cell spans).
        cursor.setPosition(m_lineAnchorPos);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    } else {
        cursor.movePosition(QTextCursor::End);
        if (!document()->isEmpty()) {
            cursor.insertBlock();
        }
    }

    // Record where this line starts before inserting HTML.
    m_lineAnchorPos = cursor.position();
    cursor.insertHtml(html);
    m_openRendered = true;
}

QString ConsoleView::buildLinePlain() const {
    QStringList byteLines;
    for (int i = 0; i < m_curBytes.size(); ++i) {
        const auto b = static_cast<unsigned char>(m_curBytes.at(i));
        QStringList parts;
        if (m_showHex) {
            parts << QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
        }
        if (m_showDec) {
            parts << QStringLiteral("%1").arg(b, 3, 10, QLatin1Char('0'));
        }
        if (m_showBin) {
            parts << QStringLiteral("%1").arg(b, 8, 2, QLatin1Char('0'));
        }
        if (m_showAscii) {
            parts << ((b >= 0x20 && b < 0x7F) ? QString(QLatin1Char(static_cast<char>(b))) : QStringLiteral("."));
        }
        byteLines << parts.join(QStringLiteral("/"));
    }
    const QString ts = QDateTime::fromMSecsSinceEpoch(m_curTs).toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString tag = m_curDir == Direction::Rx ? QStringLiteral("Rx") : QStringLiteral("Tx");
    return QStringLiteral("[%1 %2] %3").arg(ts, tag, byteLines.join(QStringLiteral(" ")));
}

void ConsoleView::finalizeLine() {
    // Log the completed line (full session, independent of the display cap).
    if (m_logFile != nullptr && !m_curBytes.isEmpty()) {
        m_logFile->write(buildLinePlain().toUtf8());
        m_logFile->write("\n");
    }
    m_curBytes.clear();
    m_openRendered = false;
    m_lineAnchorPos = -1;  // reset open-line anchor
    m_tlvTargetSize = -1;  // reset TLV state for the next packet
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

        case NewlineMode::TLV: {
            for (const char c : chunk.data) {
                beginLineIfEmpty(chunk);
                m_curBytes.append(c);

                // Once we have the full header, decode the payload length.
                if (m_tlvTargetSize == -1 && m_curBytes.size() >= m_tlvHeaderSize) {
                    const auto *hdr = reinterpret_cast<const unsigned char *>(m_curBytes.constData());
                    int payloadLen = 0;
                    if (m_tlvLenSize >= 2) {
                        // Big-endian 2-byte length field
                        payloadLen = (static_cast<int>(hdr[m_tlvLenOffset]) << 8)
                                   |  static_cast<int>(hdr[m_tlvLenOffset + 1]);
                    } else {
                        payloadLen = static_cast<int>(hdr[m_tlvLenOffset]);
                    }
                    m_tlvTargetSize = m_tlvHeaderSize + payloadLen;
                }

                if (m_tlvTargetSize > 0 && m_curBytes.size() >= m_tlvTargetSize) {
                    renderOpenLine();
                    finalizeLine();  // resets m_tlvTargetSize to -1
                }
            }
            if (!m_curBytes.isEmpty()) {
                renderOpenLine();  // show partial packet while streaming
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

        QRegularExpression regex = buildSearchRegex(text);
        if (regex.isValid() && !regex.pattern().isEmpty()) {
            while (!cursor.isNull() && !cursor.atEnd()) {
                cursor = doc->find(regex, cursor);
                if (!cursor.isNull()) {
                    QTextEdit::ExtraSelection sel;
                    sel.format = format;
                    sel.cursor = cursor;
                    selections.append(sel);
                }
            }
        } else {
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
    }
    setExtraSelections(selections);
}

QRegularExpression ConsoleView::buildSearchRegex(const QString &query) const {
    QByteArray bytes;
    QString cleanQuery = query.trimmed();

    // Check if the query is a valid hex pattern (e.g. "41 42" or "4142")
    QRegularExpression hexPattern(QStringLiteral("^(?:(?:0x)?[0-9a-fA-F]{2}\\s*)+$"));
    if (hexPattern.match(cleanQuery).hasMatch()) {
        QString hexStr = cleanQuery;
        hexStr.remove(QStringLiteral("0x"));
        hexStr.remove(QStringLiteral(" "));
        bytes = QByteArray::fromHex(hexStr.toUtf8());
    } else {
        // Otherwise treat as plain ASCII search string
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

        QString cellPattern = layers.join(QStringLiteral("/"));
        cellPatterns << QStringLiteral("\\b%1\\b").arg(cellPattern);
    }

    return QRegularExpression(cellPatterns.join(QStringLiteral("\\s+")), QRegularExpression::CaseInsensitiveOption);
}

bool ConsoleView::findQuery(const QString &query, QTextDocument::FindFlags flags) {
    if (query.isEmpty()) {
        return false;
    }

    QRegularExpression regex = buildSearchRegex(query);
    if (!regex.isValid() || regex.pattern().isEmpty()) {
        return find(query, flags);
    }

    return find(regex, flags);
}

QMimeData *ConsoleView::createMimeDataFromSelection() const {
    const QString selected = textCursor().selectedText();
    if (selected.isEmpty()) {
        return nullptr;
    }

    // QTextCursor::selectedText() returns U+2029 paragraph separator instead of standard newlines.
    QString cleaned = selected;
    cleaned.replace(QChar(0x2029), QChar('\n'));

    QStringList lines = cleaned.split(QLatin1Char('\n'));
    QStringList filteredLines;

    for (QString line : lines) {
        // Find end of metadata prefix "[HH:mm:ss.zzz Rx] "
        int idx = line.indexOf(QLatin1String("] "));
        if (idx >= 0) {
            line = line.mid(idx + 2);
        }

        QStringList finalTokens;
        QStringList tokens = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &token : tokens) {
            // For stacked cells, plain text is formatted as "HEX/DEC/BIN/ASCII"
            // Split by '/' to extract individual layers.
            QStringList layers = token.split(QLatin1Char('/'), Qt::KeepEmptyParts);
            if (layers.isEmpty()) {
                continue;
            }
            // We grab the first available layer (which is HEX if checked, else DEC/BIN/ASCII)
            finalTokens << layers.first();
        }
        if (!finalTokens.isEmpty()) {
            filteredLines << finalTokens.join(QLatin1Char(' '));
        }
    }

    auto *mime = new QMimeData();
    mime->setText(filteredLines.join(QLatin1Char('\n')));
    return mime;
}

}  // namespace aether
