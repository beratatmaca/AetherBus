/**
 * @file pcap_writer.hpp
 * @brief Thread-safe writer for @c LINKTYPE_RTAC_SERIAL capture files.
 *
 * Serialises direction-tagged byte chunks into the same pcap format that
 * @ref aether::readRtacPcap consumes for replay. The format is platform-neutral
 * (built on @c QFile), so every backend — the POSIX PTY proxy and the Windows
 * named-pipe proxy alike — records captures through this one implementation
 * instead of duplicating the byte layout.
 */
#pragma once

#include "core/serial/serial_types.hpp"

#include <QString>

#include <memory>
#include <mutex>

class QFile;

namespace aether {

/**
 * @brief Records captured chunks to a @c LINKTYPE_RTAC_SERIAL pcap file.
 *
 * All members are safe to call concurrently: @ref writePacket is invoked from a
 * backend worker thread while @ref open / @ref close / @ref isOpen may be called
 * from the owning (GUI) thread.
 */
class PcapWriter {
public:
    PcapWriter();
    ~PcapWriter();

    PcapWriter(const PcapWriter &) = delete;
    PcapWriter &operator=(const PcapWriter &) = delete;

    /**
     * @brief Open @p path for writing and emit the pcap global header.
     * @param path  Destination file (truncated if it exists).
     * @param error Optional out-param populated with a reason on failure.
     * @return @c true on success.
     */
    bool open(const QString &path, QString *error = nullptr);

    /** @brief Close the file if open (no-op otherwise). */
    void close();

    /** @brief Whether a capture file is currently open. */
    [[nodiscard]] bool isOpen() const;

    /**
     * @brief Append one direction-tagged record.
     * @param timestampMs Wall-clock capture time (ms since epoch).
     * @param dir         Stream the chunk belongs to (Tx/Rx).
     * @param data        Raw payload bytes.
     */
    void writePacket(qint64 timestampMs, Direction dir, const QByteArray &data);

private:
    mutable std::mutex m_mutex;
    std::unique_ptr<QFile> m_file;
};

}  // namespace aether
