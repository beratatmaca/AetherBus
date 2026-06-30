/**
 * @file serial_types.h
 * @brief Core value types shared between the interception backend and the GUI.
 *
 * This header is deliberately dependency-light (Qt Core only) so the backend
 * can be unit tested and reused without pulling in Qt Widgets.
 */
#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>

#include <cstdint>

namespace aether {

/**
 * @brief Direction of a captured chunk relative to the host application.
 *
 * @c Rx is data that arrived from the physical UART (peripheral -> host); @c Tx
 * is data the target application wrote to the pseudo-terminal (host ->
 * peripheral).
 */
enum class Direction : std::uint8_t {
    Rx, ///< Peripheral -> host (read from the physical UART).
    Tx, ///< Host -> peripheral (read from the PTY master).
};

/** @brief Hardware/software flow control applied to the physical link. */
enum class FlowControl : std::uint8_t {
    None,    ///< No handshake.
    RtsCts,  ///< Hardware RTS/CTS handshake (@c CRTSCTS).
    XonXoff, ///< Software XON/XOFF handshake.
};

/**
 * @brief A single timestamped, direction-tagged block of captured bytes.
 *
 * Produced by the multiplexing loop; copies are pushed onto the GUI queue for
 * rendering. Registered as a metatype so it can cross queued signal/slot
 * connections from the worker thread to the GUI thread.
 */
struct CapturedChunk {
    qint64 timestampMs = 0;        ///< Wall-clock capture time (ms since epoch).
    Direction dir = Direction::Rx; ///< Stream this chunk belongs to.
    QByteArray data;               ///< Raw bytes, exactly as seen on the wire.
};

/**
 * @brief Parameters describing how to open the physical serial device.
 *
 * Mirrors the classic termios knobs a diagnostic tool needs to expose.
 */
struct SerialConfig {
    QString device;                ///< Physical device path, e.g. @c /dev/ttyUSB0.
    QString symlinkPath;           ///< Optional symlink pointing at the slave PTY.
    int baud = 115200;             ///< Baud rate; arbitrary values allowed (Linux termios2).
    int dataBits = 8;              ///< Data bits, 5..8.
    char parity = 'N';             ///< Parity: @c 'N', @c 'E', or @c 'O'.
    int stopBits = 1;              ///< Stop bits, 1 or 2.
    FlowControl flow = FlowControl::None; ///< Handshake on the physical link.
    bool directMode = false;       ///< Direct connection (bypass the PTY proxy).
};

} // namespace aether

/// Enables @ref aether::CapturedChunk in queued signal/slot connections.
Q_DECLARE_METATYPE(aether::CapturedChunk)
