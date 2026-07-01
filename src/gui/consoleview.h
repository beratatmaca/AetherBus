// HTerm-style high-throughput console — custom-paint implementation.
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

#include "core/serial_types.h"

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

// ---------------------------------------------------------------------------
// Internal data types
// ---------------------------------------------------------------------------

/// One fully-assembled display line ready to be painted.
struct DisplayLine {
    qint64 timestampMs = 0;
    Direction dir = Direction::Rx;
    QByteArray bytes;  ///< raw payload bytes for this line

    // Pre-rendered text tokens (one string per byte per active column).
    // Columns are stored in the order: HEX, DEC, BIN, ASCII (only active
    // ones are populated).  Each element is a QStringList of width == bytes.size().
    QVector<QStringList> cols;  ///< cols[colIdx][byteIdx]
    QString prefix;             ///< "[HH:mm:ss.zzz Rx/Tx]" or "[Rx/Tx]"
    QString ascii;              ///< ASCII representation on the right
};

/// Lightweight text cursor pointing into the DisplayLine vector.
struct CursorPos {
    int line = 0;    ///< index into m_lines
    int column = 0;  ///< flat character index within the rendered line text
};

// ---------------------------------------------------------------------------
// ConsoleView
// ---------------------------------------------------------------------------

class ConsoleView : public QAbstractScrollArea {
    Q_OBJECT

public:
    /// How accumulated bytes are split into display lines.
    enum class NewlineMode : std::uint8_t {
        PerChunk,    ///< one line per captured chunk (legacy behaviour)
        Delimiter,   ///< break after a chosen byte value (e.g. 0x0A)
        FixedCount,  ///< break every N bytes
        TLV,         ///< split on length encoded in a fixed-size header
        CrLf,        ///< break on \\r, \\n, or \\r\\n
    };

    /// How the Find query text is interpreted before matching. Order matches the
    /// GUI mode combo, so an index can be cast directly to this enum.
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

    // -----------------------------------------------------------------------
    // QPlainTextEdit-compatible surface used by mainwindow.cpp
    // -----------------------------------------------------------------------
    /// Return all visible history as plain text (used by Save… action).
    [[nodiscard]] QString toPlainText() const;

    /// Minimal cursor replacement — position in lines×chars.
    [[nodiscard]] CursorPos textCursor() const { return m_cursor; }
    void setTextCursor(const CursorPos &c);

    /// Jump to start / end (mirrors QTextCursor::Start / End moves).
    void moveCursorToStart();
    void moveCursorToEnd();

    /// Find forward or backward from the current cursor position.
    /// @param query  Text (or pattern) to search for in the console buffer.
    /// @param flags  QTextDocument::FindBackward is honoured; others ignored.
    bool findQuery(const QString &query, int flags = 0);

    [[nodiscard]] bool isLogging() const { return m_logFile != nullptr; }

public slots:
    /// Enqueue a chunk for the next flush. Cheap; safe to call at high rates.
    void appendChunk(const aether::CapturedChunk &chunk);

    /// Drop all buffered and displayed content (does not reset counters).
    void clearConsole();

    /// Choose which representation columns are rendered.
    void setFormats(bool hex, bool dec, bool bin, bool ascii);

    /// Set the TLV parsing parameters.
    void setTlvParams(int headerSize, int lenOffset, int lenSize);

    /// Set the line-splitting rule.
    void setNewlineMode(NewlineMode mode, int param);

    /// When @p on is false the [HH:mm:ss.zzz] prefix is hidden.
    void setShowTimestamps(bool on);

    /// When off, the view does not auto-jump to the newest line.
    void setAutoScroll(bool on);

    /// When paused, incoming chunks are counted but not rendered until resumed.
    void setPaused(bool paused);

    /// Zero the Rx/Tx byte counters.
    void resetCounts();

    /// Begin appending every finalized line to @p path.
    bool startLogging(const QString &path);

    /// Stop and close the active log file.
    void stopLogging();

    /// Highlight all occurrences of @p text in the viewport.
    void highlightSearchText(const QString &text);

    /// Choose how the Find query is interpreted (text / hex / dec / bin / auto).
    void setSearchMode(SearchMode mode);

signals:
    /// Emitted whenever the running byte totals change.
    void countsChanged(qint64 rx, qint64 tx, qint64 rxRate, qint64 txRate);

    /// Emitted when the text selection changes; @p chars is the selected length.
    void selectionChars(int chars);

    /// Emitted after a highlight pass with the number of matches found.
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
    // -----------------------------------------------------------------------
    // Line assembly helpers
    // -----------------------------------------------------------------------
    void processChunk(const CapturedChunk &chunk);
    void beginLineIfEmpty(const CapturedChunk &chunk);
    void commitOpenLine();  ///< push m_openLine into m_lines and reset
    void renderOpenLine();  ///< re-render m_openLine.cols from m_openLine.bytes
    void finalizeLine();    ///< commit + log

    DisplayLine buildLine(Direction dir, qint64 tsMs, const QByteArray &bytes) const;
    [[nodiscard]] QString lineToPlain(const DisplayLine &dl) const;

    // -----------------------------------------------------------------------
    // Search helpers
    // -----------------------------------------------------------------------
    /// Build a regex matching @p bytes against the active display columns.
    [[nodiscard]] QRegularExpression buildByteRegex(const QByteArray &bytes) const;
    /// Resolve the query into a matcher per @ref m_searchMode. Returns a regex to
    /// match; if it is empty, @p useLiteral says whether the caller should fall
    /// back to a literal substring search (true) or treat it as no match (false,
    /// e.g. an explicit hex/dec/bin query that failed to parse).
    [[nodiscard]] QRegularExpression searchRegexForQuery(const QString &query, bool &useLiteral) const;
    /// Flat plain-text representation of a line for search matching.
    [[nodiscard]] QString lineSearchText(const DisplayLine &dl) const;

    // -----------------------------------------------------------------------
    // Scroll / layout helpers
    // -----------------------------------------------------------------------
    void updateScrollBars();
    int lineHeight() const;
    int firstVisibleLine() const;
    int visibleLineCount() const;

    /// Translate a viewport pixel position to (line, charOffset).
    CursorPos posFromPoint(QPoint pt) const;
    /// Pixel x offset of character @p col in line @p li (approx, monospace).
    int xOfChar(int li, int col) const;
    /// Full rendered plain text of a line for measuring selection.
    QString fullLineText(int li) const;

    // -----------------------------------------------------------------------
    // Clipboard / mime helpers
    // -----------------------------------------------------------------------
    [[nodiscard]] QString selectedText() const;
    void copySelectionToClipboard(int layerIndex) const;

    // -----------------------------------------------------------------------
    // Reapply history after a settings change
    // -----------------------------------------------------------------------
    void reapplyHistory();

    // -----------------------------------------------------------------------
    // Data
    // -----------------------------------------------------------------------

    // Pending capture queue + flush timer.
    QVector<CapturedChunk> m_pending;
    QTimer *m_flushTimer;

    // Finalized display lines (ring buffer).
    QVector<DisplayLine> m_lines;

    // Raw chunk history for reapplyHistory().
    QVector<CapturedChunk> m_history;

    // Currently open (not-yet-finalized) line being assembled.
    DisplayLine m_openLine;
    bool m_openLineActive = false;  ///< true while m_openLine is in m_lines tail

    // CrLf mode state.
    bool m_pendingCr = false;

    // TLV parsing state.
    int m_tlvTargetSize = -1;

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

    // Running byte counters.
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

    // Cursor (for find / wrap-around).
    CursorPos m_cursor;

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

    // Optional full-session log file (owned; parented to this).
    QFile *m_logFile = nullptr;

    // Pixel scroll offset within a line (horizontal).
    int m_hScroll = 0;

    static constexpr int kLeftPad = 4;
    static constexpr int kFindBackward = 1;
};

}  // namespace aether
