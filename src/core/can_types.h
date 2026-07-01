/**
 * @file can_types.h
 * @brief Value types describing a SocketCAN connection.
 *
 * Dependency-light (Qt Core only) so the CAN backend can be unit tested without
 * pulling in Qt Widgets, mirroring @ref serial_types.h.
 */
#pragma once

#include <QString>
#include <QVector>

namespace aether {

/**
 * @brief A single SocketCAN hardware/software receive filter.
 *
 * A frame is accepted when @c (frame.can_id & mask) == (id & mask). Multiple
 * filters are OR-combined by the kernel. @c invert flips an individual rule.
 */
struct CanFilter {
    quint32 id = 0;          ///< Identifier to match (flag bits masked off).
    quint32 mask = 0x7FF;    ///< Match mask; default matches a full 11-bit id.
    bool extended = false;   ///< Interpret @c id as a 29-bit (EFF) identifier.
    bool invert = false;     ///< Invert this rule (CAN_INV_FILTER).
};

/**
 * @brief Parameters describing how to open a SocketCAN interface.
 *
 * The bit rate is NOT set here — it is an interface property configured out of
 * band (e.g. @c "ip link set can0 type can bitrate 500000"). This struct only
 * covers the per-socket knobs a monitor needs.
 */
struct CanConfig {
    QString iface;               ///< Interface name, e.g. @c can0 / @c vcan0.
    QVector<CanFilter> filters;  ///< Receive filters; empty => accept every frame.
    bool fdMode = true;          ///< Enable CAN-FD frames (CAN_RAW_FD_FRAMES).
    bool loopback = true;        ///< Local loopback to other sockets (CAN_RAW_LOOPBACK).
    bool receiveOwn = false;     ///< Also receive our own sent frames (CAN_RAW_RECV_OWN_MSGS).
    bool errorFrames = true;     ///< Subscribe to bus error frames (CAN_RAW_ERR_FILTER).

    /**
     * @brief Validate the interface name before it is handed to the kernel.
     * @return An empty string when valid; otherwise a human-readable reason.
     */
    [[nodiscard]] QString validate() const {
        if (iface.isEmpty()) {
            return QStringLiteral("CAN interface name is empty.");
        }
        if (iface.size() >= 16) {  // IFNAMSIZ
            return QStringLiteral("CAN interface name is too long (max 15 chars).");
        }
        return {};
    }
};

}  // namespace aether
