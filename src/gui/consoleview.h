// HTerm-style high-throughput console.
//
// Captured chunks are queued and flushed to the text document on a fixed 60 Hz
// timer rather than on every signal, so megabaud streams cannot drown the UI
// thread in per-byte draw calls. A block-count ceiling bounds memory by
// discarding the oldest history.
//
// Bytes are reassembled into display lines according to a configurable newline
// rule (per-chunk, on a delimiter byte, or every N bytes) and a direction
// change always starts a fresh line. Each line can be shown simultaneously in
// any combination of HEX / DECIMAL / BINARY / ASCII columns.
#pragma once

#include "core/serial_types.h"

#include <QPlainTextEdit>
#include <QVector>
#include <QRegularExpression>

#include <cstdint>

class QTimer;
class QFile;

#include <QMimeData>

namespace aether {

class ConsoleView : public QPlainTextEdit {
    Q_OBJECT

public:
    /// How accumulated bytes are split into display lines.
    enum class NewlineMode : std::uint8_t {
        PerChunk,    ///< one line per captured chunk (legacy behaviour)
        Delimiter,   ///< break after a chosen byte value (e.g. 0x0A)
        FixedCount,  ///< break every N bytes
        TLV,         ///< split on length encoded in a fixed-size header (Type-Length-Value)
        CrLf,        ///< break on \r, \n, or \r\n (CR+LF counted as one newline)
    };

    explicit ConsoleView(QWidget *parent = nullptr);

    [[nodiscard]] qint64 rxCount() const { return m_rx; }
    [[nodiscard]] qint64 txCount() const { return m_tx; }

public slots:
    /// Enqueue a chunk for the next flush. Cheap; safe to call at high rates.
    void appendChunk(const aether::CapturedChunk &chunk);

    /// Drop all buffered and displayed content (does not reset counters).
    void clearConsole();

    /// Choose which representation layers are stacked.
    void setFormats(bool hex, bool dec, bool bin, bool ascii);

    /// Find search text dynamically translating hex/ascii.
    bool findQuery(const QString &query, QTextDocument::FindFlags flags);

    /// Set the TLV parsing parameters.
    void setTlvParams(int headerSize, int lenOffset, int lenSize);

    /// Set the line-splitting rule. @p param is the delimiter byte value for
    /// Delimiter mode, or the byte count for FixedCount mode.
    void setNewlineMode(NewlineMode mode, int param);


    /// When off, the view does not auto-jump to the newest line.
    void setAutoScroll(bool on);

    /// When paused, incoming chunks are still counted but not rendered until
    /// resumed.
    void setPaused(bool paused);

    /// Zero the Rx/Tx byte counters.
    void resetCounts();

    /// Begin appending every finalized line (as plain text) to @p path. This is
    /// independent of the on-screen history ceiling, so it captures the full
    /// session. Returns false if the file cannot be opened.
    bool startLogging(const QString &path);

    /// Stop and close the active log file (if any).
    void stopLogging();

    /// Highlight all occurrences of search text in the viewport.
    void highlightSearchText(const QString &text);

    [[nodiscard]] bool isLogging() const { return m_logFile != nullptr; }

signals:
    /// Emitted whenever the running byte totals change.
    void countsChanged(qint64 rx, qint64 tx);

    /// Emitted when the text selection changes; @p chars is the selected length.
    void selectionChars(int chars);

protected:
    QMimeData *createMimeDataFromSelection() const override;

private slots:
    void flush();

private:
    QRegularExpression buildSearchRegex(const QString &query) const;
    void reapplyHistory();
    void processChunk(const CapturedChunk &chunk);
    void beginLineIfEmpty(const CapturedChunk &chunk);
    void renderOpenLine();
    void finalizeLine();
    [[nodiscard]] QString buildLineHtml() const;
    [[nodiscard]] QString buildLinePlain() const;

    // Pending capture queue + flush timer.
    QVector<CapturedChunk> m_pending;
    QTimer *m_flushTimer;

    // Current open (not-yet-finalized) line being assembled.
    QByteArray m_curBytes;
    Direction m_curDir = Direction::Rx;
    qint64 m_curTs = 0;
    bool m_openRendered = false;  ///< true if the document tail contains m_curBytes
    int  m_lineAnchorPos = -1;    ///< character position in document where open line starts

    // Display options.
    bool m_showHex = true;
    bool m_showDec = false;
    bool m_showBin = false;
    bool m_showAscii = true;
    NewlineMode m_mode = NewlineMode::Delimiter;
    int m_newlineParam = 0x0A;

    // TLV header-based splitting.
    int m_tlvHeaderSize = 3;   ///< total header byte count
    int m_tlvLenOffset  = 1;   ///< byte offset of length field inside header
    int m_tlvLenSize    = 1;   ///< width of length field in bytes (1 or 2 BE)
    int m_tlvTargetSize = -1;  ///< computed full packet size; -1 = waiting for header

    // CrLf mode: absorb the \n of a \r\n pair so it doesn't produce a second line.
    bool m_pendingCr = false;

    bool m_autoScroll = true;
    bool m_paused = false;
    QVector<CapturedChunk> m_history;

    // Running byte counters.
    qint64 m_rx = 0;
    qint64 m_tx = 0;

    // Optional full-session log file (owned; parented to this).
    QFile *m_logFile = nullptr;
};

}  // namespace aether
