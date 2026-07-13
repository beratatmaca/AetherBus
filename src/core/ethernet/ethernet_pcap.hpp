/**
 * @file ethernet_pcap.hpp
 * @brief Reads a classic (non-pcapng) LINKTYPE_ETHERNET pcap file.
 *
 * Dependency-light (Qt Core only) so it can be unit tested without Qt Widgets,
 * mirroring @ref aether::readRtacPcap for the serial/CAN transports.
 */
#pragma once

#include "core/serial/serial_types.hpp"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>

class QFile;

namespace aether {

/**
 * @brief Parse a classic pcap file with LINKTYPE_ETHERNET (1) frames.
 *
 * This is the format @ref EthernetSessionWidget::onSavePcap() itself writes,
 * and what tcpdump/Wireshark produce by default. The file format carries no
 * per-record direction, so every returned chunk is tagged @c Direction::Tx
 * (the caller replays them as outgoing traffic).
 *
 * @param path  Path to the capture file.
 * @param error Optional out-param populated with a human-readable reason on
 *              failure; ignored when @c nullptr.
 * @return The captured frames in file order, or @c std::nullopt on a
 *         malformed/unreadable file or an unsupported format (pcapng, or a
 *         link type other than Ethernet).
 */
std::optional<QVector<CapturedChunk>> readEthernetPcap(const QString &path, QString *error = nullptr);

/**
 * @brief Incrementally writes a classic LINKTYPE_ETHERNET pcap file.
 *
 * Same on-disk layout @ref readEthernetPcap parses back, factored out so both
 * a one-shot "Save PCAP…" export and a continuously-streaming "Capture"
 * toggle can share one implementation instead of duplicating the byte layout.
 * Mirrors the shape of @ref aether::PcapWriter (the serial/CAN capture
 * writer), but for Ethernet's plain-Ethernet-frame format.
 */
class EthernetPcapWriter {
public:
    EthernetPcapWriter();
    ~EthernetPcapWriter();

    EthernetPcapWriter(const EthernetPcapWriter &) = delete;
    EthernetPcapWriter &operator=(const EthernetPcapWriter &) = delete;

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
     * @brief Append one Ethernet frame record.
     * @param timestampMs Wall-clock capture time (ms since epoch).
     * @param data        Raw frame bytes.
     */
    void writePacket(qint64 timestampMs, const QByteArray &data);

private:
    std::unique_ptr<QFile> m_file;
};

}  // namespace aether
