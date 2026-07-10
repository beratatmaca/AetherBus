/**
 * @file ethernet_pcap.hpp
 * @brief Reads a classic (non-pcapng) LINKTYPE_ETHERNET pcap file.
 *
 * Dependency-light (Qt Core only) so it can be unit tested without Qt Widgets,
 * mirroring @ref aether::readRtacPcap for the serial/CAN transports.
 */
#pragma once

#include "core/serial/serial_types.hpp"

#include <QString>
#include <QVector>

#include <optional>

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

}  // namespace aether
