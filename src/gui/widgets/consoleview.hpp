// High-throughput console — custom-paint implementation.
//
// Architecture
// ============
// Instead of feeding a QTextDocument with per-byte HTML <span> nodes (which
// gets expensive at ≥115 200 baud), the view keeps a plain
// QVector<DisplayLine> ring buffer and renders only the lines that are
// currently visible on screen inside paintEvent().  The DOM is gone entirely.
//
// Captured chunks are queued and flushed on a fixed 60 Hz timer so the UI
// thread is never saturated.  The rolling history ceiling (kMaxLines) is
// enforced on the ring buffer rather than on a QTextDocument block count.
//
// Public API compatibility
// ========================
// The class preserves every signal, slot, and method that mainwindow.cpp
// calls so the UI layer requires no changes.  The QTextCursor / QTextDocument
// surface is replaced by lightweight equivalents (CursorPos, findQuery,
// highlightSearchText, toPlainText).
#pragma once

#include "core/serial/serial_types.hpp"

#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QFont>
#include <QFontMetrics>
#include <QRegularExpression>
#include <QVector>

#include <cstdint>

class QTimer;
class QFile;
class QContextMenuEvent;
class QKeyEvent;
class QMimeData;

namespace aether {

/** @brief One fully-assembled display line ready to be painted. */
struct DisplayLine {
    qint64 timestampMs = 0;
    Direction dir = Direction::Rx;
    QByteArray bytes;  ///< raw payload bytes for this line

    QVector<QStringList> cols;  ///< cols[colIdx][byteIdx] — pre-rendered text tokens (one string per byte per active column).
                                ///< Columns are stored in the order: HEX, DEC, BIN, ASCII (only active ones are populated).
                                ///< Each element is a QStringList of width == bytes.size().
    QString prefix;             ///< "[HH:mm:ss.zzz Rx/Tx]" or "[Rx/Tx]" (+ frame header)
    QString ascii;              ///< ASCII representation on the right

    mutable QString plainCache;  ///< Lazily-filled full plain-text form (see ConsoleView::lineSearchText). Empty means
                                 ///< uncached — a real line's text always has a non-empty prefix. Never populated by
                                 ///< buildLine() (that would add a string join to the per-chunk hot path); filled on
                                 ///< first search/copy use instead.

    // Optional framing metadata mirrored from the source chunk (CAN etc.).
    bool isFrame = false;
    quint32 frameId = 0;
    quint16 frameFlags = 0;
};

/** @brief Lightweight text cursor pointing into the DisplayLine vector. */
struct CursorPos {
    int line = 0;    ///< index into m_lines
    int column = 0;  ///< flat character index within the rendered line text
};

class ConsoleView : public QAbstractScrollArea {
    Q_OBJECT

public:
    /** @brief How accumulated bytes are split into display lines. */
    enum class NewlineMode : std::uint8_t {
        PerChunk,    ///< one line per captured chunk (legacy behaviour)
        Delimiter,   ///< break after a chosen byte value (e.g. 0x0A)
        FixedCount,  ///< break every N bytes
        TLV,         ///< split on length encoded in a fixed-size header
        CrLf,        ///< break on \\r, \\n, or \\r\\n
        Frame,       ///< one line per captured frame, rendered with its frame header (CAN)
    };

    /**
     * @brief How the Find query text is interpreted before matching.
     *
     * Order matches the GUI mode combo, so an index can be cast directly to this enum.
     */
    enum class SearchMode : std::uint8_t {
        Auto,  ///< guess: hex-looking input is bytes, otherwise literal text
        Text,  ///< literal (case-insensitive) substring of the rendered line
        Hex,   ///< space/comma-separated hex byte values (e.g. "41 42 0D")
        Dec,   ///< decimal byte values 0..255 (e.g. "65 66 13")
        Bin,   ///< 8-bit binary byte values (e.g. "01000001 01000010")
    };

    explicit ConsoleView(QWidget *parent = nullptr);

    [[nodiscard]] qint64 rxCount() const { return m_rx; }
    [[nodiscard]] qint64 txCount() const { return m_tx; }

    /** @brief Return all visible history as plain text (used by Save… action). */
    [[nodiscard]] QString toPlainText() const;

    /** @brief Minimal cursor replacement — position in lines×chars. */
    [[nodiscard]] CursorPos textCursor() const { return m_cursor; }
    void setTextCursor(const CursorPos &c);

    /** @brief Jump to start / end (mirrors QTextCursor::Start / End moves). */
    void moveCursorToStart();
    void moveCursorToEnd();

    /**
     * @brief Find forward or backward from the current cursor position.
     * @param query  Text (or pattern) to search for in the console buffer.
     * @param flags  QTextDocument::FindBackward is honoured; others ignored.
     */
    bool findQuery(const QString &query, int flags = 0);

    [[nodiscard]] bool isLogging() const { return m_logFile != nullptr; }

public slots:
    /** @brief Enqueue a chunk for the next flush. Cheap; safe to call at high rates. */
    void appendChunk(const aether::CapturedChunk &chunk);

    /** @brief Drop all buffered and displayed content (does not reset counters). */
    void clearConsole();

    /** @brief Choose which representation columns are rendered. */
    void setFormats(bool hex, bool dec, bool bin, bool ascii);

    /** @brief Set the TLV parsing parameters. */
    void setTlvParams(int headerSize, int lenOffset, int lenSize);

    /** @brief Set the line-splitting rule. */
    void setNewlineMode(NewlineMode mode, int param);

    /** @brief When @p on is false the [HH:mm:ss.zzz] prefix is hidden. */
    void setShowTimestamps(bool on);

    /** @brief When off, the view does not auto-jump to the newest line. */
    void setAutoScroll(bool on);

    /** @brief When paused, incoming chunks are counted but not rendered until resumed. */
    void setPaused(bool paused);

    /** @brief Zero the Rx/Tx byte counters. */
    void resetCounts();

    /** @brief Begin appending every finalized line to @p path. */
    bool startLogging(const QString &path);

    /** @brief Stop and close the active log file. */
    void stopLogging();

    /** @brief Highlight all occurrences of @p text in the viewport. */
    void highlightSearchText(const QString &text);

    /** @brief Choose how the Find query is interpreted (text / hex / dec / bin / auto). */
    void setSearchMode(SearchMode mode);

    [[nodiscard]] QString selectedText() const;

    /**
     * @brief Raw payload bytes covered by the current selection, mapped back from the
     * underlying per-line data (never the rendered timestamps/ASCII gutter), so
     * callers get exactly the bytes the user highlighted across one or more lines.
     */
    [[nodiscard]] QByteArray selectedBytes() const;

signals:
    /** @brief Emitted whenever the running byte totals change. */
    void countsChanged(qint64 rx, qint64 tx, qint64 rxRate, qint64 txRate);

    /** @brief Emitted when the text selection changes; @p chars is the selected length. */
    void selectionChars(int chars);

    /** @brief Emitted after a highlight pass with the number of matches found. */
    void searchMatchCount(int count);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void flush();

private:
    void processChunk(const CapturedChunk &chunk);
    void beginLineIfEmpty(const CapturedChunk &chunk);
    /** @brief push m_openLine into m_lines and reset */
    void commitOpenLine();
    /** @brief re-render m_openLine.cols from m_openLine.bytes */
    void renderOpenLine();
    /** @brief commit + log */
    void finalizeLine();

    DisplayLine buildLine(Direction dir, qint64 tsMs, const QByteArray &bytes, bool isFrame = false, quint32 frameId = 0,
                          quint16 frameFlags = 0) const;
    /** @brief Compose the per-frame header shown after the timestamp, e.g. "123 [4] R". */
    [[nodiscard]] static QString frameHeader(quint32 id, quint16 flags, int payloadLen);
    [[nodiscard]] QString lineToPlain(const DisplayLine &dl) const;

    /** @brief Build a regex matching @p bytes against the active display columns. */
    [[nodiscard]] QRegularExpression buildByteRegex(const QByteArray &bytes) const;
    /**
     * @brief Resolve the query into a matcher per @ref m_searchMode.
     *
     * Returns a regex to match; if it is empty, @p useLiteral says whether the
     * caller should fall back to a literal substring search (true) or treat it
     * as no match (false, e.g. an explicit hex/dec/bin query that failed to parse).
     */
    [[nodiscard]] QRegularExpression searchRegexForQuery(const QString &query, bool &useLiteral) const;
    /** @brief Flat plain-text representation of a line for search matching. */
    [[nodiscard]] QString lineSearchText(const DisplayLine &dl) const;

    void updateScrollBars();
    /**
     * @brief Content-space x (pixels, pre-scroll) at which @p dl's hex region ends,
     * i.e. where its "  |  " separator would begin without alignment.
     */
    [[nodiscard]] int hexRegionEndX(const DisplayLine &dl) const;
    int lineHeight() const;
    int firstVisibleLine() const;
    int visibleLineCount() const;

    /** @brief Translate a viewport pixel position to (line, charOffset). */
    CursorPos posFromPoint(QPoint pt) const;
    /** @brief Pixel x offset of character @p col in line @p li (approx, monospace). */
    int xOfChar(int li, int col) const;
    /** @brief Full rendered plain text of a line for measuring selection. */
    QString fullLineText(int li) const;

    void copySelectionToClipboard(int layerIndex) const;

    void reapplyHistory();

    // Pending capture queue + flush timer.
    QVector<CapturedChunk> m_pending;
    QTimer *m_flushTimer;

    QVector<DisplayLine> m_lines;  ///< Finalized display lines (ring buffer).

    QVector<CapturedChunk> m_history;  ///< Raw chunk history for reapplyHistory().

    DisplayLine m_openLine;  ///< Currently open (not-yet-finalized) line being assembled.
    bool m_openLineActive = false;  ///< true while m_openLine is in m_lines tail

    bool m_pendingCr = false;  ///< CrLf mode state.

    int m_tlvTargetSize = -1;  ///< TLV parsing state.

    // Display options.
    bool m_showHex = true;
    bool m_showDec = false;
    bool m_showBin = false;
    bool m_showAscii = true;
    bool m_showTimestamps = true;

    NewlineMode m_mode = NewlineMode::Delimiter;
    int m_newlineParam = 0x0A;

    int m_tlvHeaderSize = 3;
    int m_tlvLenOffset = 1;
    int m_tlvLenSize = 1;

    bool m_autoScroll = true;
    bool m_paused = false;

    // Running byte counters; countsChanged is coalesced into flush() so the
    // labels update at most once per frame, not once per chunk.
    bool m_countsDirty = false;
    qint64 m_rx = 0;
    qint64 m_tx = 0;

    // Throughput rate tracking.
    qint64 m_rxWindow = 0;
    qint64 m_txWindow = 0;
    qint64 m_rxRate = 0;
    qint64 m_txRate = 0;
    QElapsedTimer m_rateTimer;

    // Font metrics (fixed-width font).
    QFont m_font;
    int m_charW = 8;   ///< character cell width in pixels
    int m_lineH = 16;  ///< line height in pixels (ascent+descent+leading)
    int m_fontAscent = 12;

    int m_asciiSepCol = 0;  ///< Shared content-space x (pre-scroll) where the ASCII "  |  " separator begins, so pipes
                            ///< align into a vertical column across lines. Widest hex-region end among lines that
                            ///< render a separator.

    CursorPos m_cursor;  ///< Cursor (for find / wrap-around).

    // Selection (anchor → active end).
    CursorPos m_selAnchor;
    CursorPos m_selEnd;
    bool m_hasSelection = false;
    bool m_mouseSelecting = false;

    // Search highlights: list of (line, startChar, length).
    struct SearchHit {
        int line;
        int start;
        int len;
    };
    QVector<SearchHit> m_searchHits;
    QString m_searchText;
    SearchMode m_searchMode = SearchMode::Auto;

    QFile *m_logFile = nullptr;  ///< Optional full-session log file (owned; parented to this).

    int m_hScroll = 0;  ///< Pixel scroll offset within a line (horizontal).

    static constexpr int kLeftPad = 4;
    static constexpr int kFindBackward = 1;
};

}  // namespace aether
