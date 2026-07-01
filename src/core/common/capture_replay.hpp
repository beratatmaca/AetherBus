/**
 * @file capture_replay.h
 * @brief Offline replay of previously captured traffic.
 *
 * The live backend (@ref aether::PtyProxy) can record traffic to a
 * @c LINKTYPE_RTAC_SERIAL pcap file via @c startCapture(). This module reads
 * such a file back into @ref aether::CapturedChunk objects and, via
 * @ref aether::CaptureReplayer, feeds them through the same rendering path the
 * live capture uses — preserving the original per-packet timestamps and timing.
 *
 * Dependency-light (Qt Core only) so it can be unit tested without Qt Widgets.
 */
#pragma once

#include "core/serial/serial_types.hpp"

#include <QObject>
#include <QString>
#include <QVector>

#include <optional>

class QTimer;

namespace aether {

/**
 * @brief Parse a @c LINKTYPE_RTAC_SERIAL pcap file into ordered chunks.
 *
 * Reads the file produced by @ref PtyProxy::startCapture(): validates the pcap
 * global header, then walks each record decoding its timestamp, direction (from
 * the RTAC pseudo-header event byte) and raw payload.
 *
 * @param path  Path to the capture file.
 * @param error Optional out-param populated with a human-readable reason on
 *              failure; ignored when @c nullptr.
 * @return The captured chunks in file order, or @c std::nullopt on a malformed
 *         or unreadable file.
 */
std::optional<QVector<CapturedChunk>> readRtacPcap(const QString &path, QString *error = nullptr);

/**
 * @brief Replays a loaded capture, emitting one chunk at a time.
 *
 * Chunks are emitted in order, paced by the original inter-packet gaps (clamped
 * so a long idle stretch cannot stall the replay). Each emitted chunk keeps its
 * original timestamp, so the console renders the capture's real times.
 */
class CaptureReplayer : public QObject {
    Q_OBJECT

public:
    explicit CaptureReplayer(QObject *parent = nullptr);
    ~CaptureReplayer() override;

    /**
     * @brief Load a capture file for replay.
     * @param path  Path to a @c LINKTYPE_RTAC_SERIAL pcap file.
     * @param error Optional failure reason (see @ref readRtacPcap).
     * @return @c true if the file parsed and at least zero chunks were loaded.
     */
    bool load(const QString &path, QString *error = nullptr);

    /** @brief Number of chunks currently loaded for replay. */
    [[nodiscard]] int chunkCount() const { return static_cast<int>(m_chunks.size()); }

    /** @brief Whether a replay is currently in progress. */
    [[nodiscard]] bool isReplaying() const { return m_replaying; }

    /** @brief Start (or restart) replaying the loaded chunks from the beginning. */
    void start();

    /** @brief Stop an in-progress replay and reset to the beginning. */
    void stop();

signals:
    /** @brief Emitted for each replayed chunk, carrying its original timestamp. */
    void chunkReplayed(const aether::CapturedChunk &chunk);

    /** @brief Emitted once the last chunk has been replayed. */
    void finished();

private:
    void scheduleNext();  ///< Arm the timer for the next chunk (or finish).
    void emitCurrent();   ///< Emit the chunk at @c m_index and advance.

    /// Upper bound on a single inter-packet wait, so idle gaps don't stall replay.
    static constexpr int kMaxGapMs = 2000;

    QVector<CapturedChunk> m_chunks;
    int m_index = 0;
    bool m_replaying = false;
    QTimer *m_timer = nullptr;
};

}  // namespace aether
