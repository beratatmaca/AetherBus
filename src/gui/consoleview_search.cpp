#include "gui/consoleview.h"
#include <QScrollBar>
#include <QMenu>
#include <QContextMenuEvent>
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>

namespace aether {

static const QColor kSelBg(48, 63, 159, 128);  // Semi-transparent Indigo

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
                    m_searchHits.append(SearchHit{li, static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength())});
                }
            } else {
                int pos = 0;
                while ((pos = lineText.indexOf(text, pos, Qt::CaseInsensitive)) >= 0) {
                    m_searchHits.append(SearchHit{li, pos, static_cast<int>(text.length())});
                    pos += text.length();
                }
            }
        }
    }
    viewport()->update();
}

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

void ConsoleView::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Copy)) {
        copySelectionToClipboard(0);
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void ConsoleView::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);

    QAction *actSelectAll = menu.addAction(QStringLiteral("Select all"));
    connect(actSelectAll, &QAction::triggered, this, [this] {
        if (!m_lines.isEmpty()) {
            m_selAnchor = CursorPos{0, 0};
            m_selEnd = CursorPos{static_cast<int>(m_lines.size() - 1), static_cast<int>(fullLineText(m_lines.size() - 1).length())};
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
        connect(rawHex, &QAction::triggered, this, [this] { copySelectionToClipboard(0); });
    }

    menu.exec(event->globalPos());
}

QString ConsoleView::selectedText() const {
    if (!m_hasSelection || m_lines.isEmpty()) {
        return {};
    }

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
        return {};
    }

    QStringList cellPatterns;
    for (char byte : bytes) {
        const auto b = static_cast<unsigned char>(byte);
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
        if (m_showAscii && layers.isEmpty()) {
            QString asc = (b >= 0x20 && b < 0x7F) ? QString(QLatin1Char(static_cast<char>(b))) : QStringLiteral(".");
            layers << QRegularExpression::escape(asc);
        }

        cellPatterns << QStringLiteral("\\b%1\\b").arg(layers.join(QLatin1Char('/')));
    }

    return QRegularExpression(cellPatterns.join(QStringLiteral("\\s+")), QRegularExpression::CaseInsensitiveOption);
}

}  // namespace aether
