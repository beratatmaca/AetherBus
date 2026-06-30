// Core value types shared between the interception backend and the GUI.
//
// This header is deliberately dependency-light (Qt Core only) so the backend
// can be unit tested and reused without pulling in Qt Widgets.
#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>

#include <cstdint>

namespace aether {

/// Direction of a captured chunk relative to the host application under test.
///
///  - Rx: bytes that arrived from the physical UART (peripheral -> host).
///  - Tx: bytes the target application wrote to the pseudo-terminal (host ->
///    peripheral).
enum class Direction : std::uint8_t { Rx, Tx };

/// Hardware/software flow control applied to the physical link.
enum class FlowControl : std::uint8_t { None, RtsCts, XonXoff };

/// A single timestamped, direction-tagged block of bytes pulled from the
/// multiplexing loop. Copies are pushed onto the GUI queue for rendering.
struct CapturedChunk {
    qint64 timestampMs = 0;         ///< Wall-clock capture time (ms since epoch).
    Direction dir = Direction::Rx;  ///< Stream this chunk belongs to.
    QByteArray data;                ///< Raw bytes, exactly as seen on the wire.
};

/// Parameters describing how to open the physical serial device. Mirrors the
/// classic termios knobs a diagnostic tool needs to expose.
struct SerialConfig {
    QString device;                        ///< Physical device path, e.g. "/dev/ttyUSB0".
    QString symlinkPath;                   ///< Optional symlink to point at the slave PTY.
    int baud = 115200;                     ///< Baud rate; arbitrary values allowed (Linux termios2).
    int dataBits = 8;                      ///< 5..8.
    char parity = 'N';                     ///< 'N', 'E', or 'O'.
    int stopBits = 1;                      ///< 1 or 2.
    FlowControl flow = FlowControl::None;  ///< Handshake on the physical link.
    bool directMode = false;               ///< Direct connection mode (bypass PTY proxy).
};

}  // namespace aether

// Allow CapturedChunk to travel across queued signal/slot connections (i.e.
// from the backend worker thread to the GUI thread).
Q_DECLARE_METATYPE(aether::CapturedChunk)
