/**
 * @file serial_types.hpp
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
    Rx,  ///< Peripheral -> host (read from the physical UART).
    Tx,  ///< Host -> peripheral (read from the PTY master).
};

/**
 * @brief Per-frame attributes for framed transports (e.g. CAN).
 *
 * Stored as a bitmask in @ref CapturedChunk::frameFlags. Stream transports such
 * as the serial proxy leave the mask at zero and never set @c isFrame.
 */
enum FrameFlag : std::uint8_t {
    FrameExtendedId = 1U << 0,     ///< 29-bit identifier (CAN EFF).
    FrameRemote = 1U << 1,         ///< Remote-transmission-request frame (CAN RTR).
    FrameError = 1U << 2,          ///< Error frame (CAN ERR).
    FrameFd = 1U << 3,             ///< CAN-FD frame (payload up to 64 bytes).
    FrameBitRateSwitch = 1U << 4,  ///< CAN-FD bit-rate switch (BRS).
    FrameErrorStateInd = 1U << 5,  ///< CAN-FD error-state indicator (ESI).
};

/** @brief Hardware/software flow control applied to the physical link. */
enum class FlowControl : std::uint8_t {
    None,     ///< No handshake.
    RtsCts,   ///< Hardware RTS/CTS handshake (@c CRTSCTS).
    XonXoff,  ///< Software XON/XOFF handshake.
};

/** @brief Parity checking applied to each frame on the physical link. */
enum class Parity : std::uint8_t {
    None,  ///< No parity bit (@c 'N').
    Even,  ///< Even parity (@c 'E').
    Odd,   ///< Odd parity (@c 'O').
};

/**
 * @brief Single-letter code for a parity mode, e.g. for compact "8N1" status.
 * @return @c 'N', @c 'E', or @c 'O'.
 */
constexpr char parityCode(Parity parity) noexcept {
    switch (parity) {
        case Parity::Even:
            return 'E';
        case Parity::Odd:
            return 'O';
        case Parity::None:
            break;
    }
    return 'N';
}

/**
 * @brief A single timestamped, direction-tagged block of captured bytes.
 *
 * Produced by the multiplexing loop; copies are pushed onto the GUI queue for
 * rendering. Registered as a metatype so it can cross queued signal/slot
 * connections from the worker thread to the GUI thread.
 */
struct CapturedChunk {
    qint64 timestampMs = 0;         ///< Wall-clock capture time (ms since epoch).
    Direction dir = Direction::Rx;  ///< Stream this chunk belongs to.
    QByteArray data;                ///< Raw bytes / frame payload, exactly as seen on the wire.

    // --- Optional framing metadata. Used by framed transports such as CAN;
    //     stream transports (serial) leave these at their defaults. ---
    bool isFrame = false;    ///< When true, @c frameId / @c frameFlags are meaningful.
    quint32 frameId = 0;     ///< Frame identifier (e.g. CAN id, with flag bits masked off).
    quint16 frameFlags = 0;  ///< Bitmask of @ref FrameFlag values.
};

/**
 * @brief Parameters describing how to open the physical serial device.
 *
 * Mirrors the classic termios knobs a diagnostic tool needs to expose.
 */
struct SerialConfig {
    QString device;                        ///< Physical device path, e.g. @c /dev/ttyUSB0.
    QString symlinkPath;                   ///< Optional symlink pointing at the slave PTY.
    int baud = 115200;                     ///< Baud rate; arbitrary values allowed (Linux termios2).
    int dataBits = 8;                      ///< Data bits, 5..8.
    Parity parity = Parity::None;          ///< Parity checking mode.
    int stopBits = 1;                      ///< Stop bits, 1 or 2.
    FlowControl flow = FlowControl::None;  ///< Handshake on the physical link.
    bool directMode = false;               ///< Direct connection (bypass the PTY proxy).

    /**
     * @brief Validate field ranges before the config is handed to termios.
     *
     * Guards against out-of-range values that would otherwise be silently
     * coerced (e.g. an unexpected @c dataBits falling through to @c CS8).
     * @return An empty string when valid; otherwise a human-readable reason.
     */
    [[nodiscard]] QString validate() const {
        if (baud <= 0) {
            return QStringLiteral("Baud rate must be positive (got %1).").arg(baud);
        }
        if (dataBits < 5 || dataBits > 8) {
            return QStringLiteral("Data bits must be 5..8 (got %1).").arg(dataBits);
        }
        if (stopBits != 1 && stopBits != 2) {
            return QStringLiteral("Stop bits must be 1 or 2 (got %1).").arg(stopBits);
        }
        return {};
    }
};

}  // namespace aether

/** @brief Enables @ref aether::CapturedChunk in queued signal/slot connections. */
Q_DECLARE_METATYPE(aether::CapturedChunk)
/** @brief Enables @ref aether::Direction as a queued signal/slot argument. */
Q_DECLARE_METATYPE(aether::Direction)
/** @brief Enables @ref aether::Parity as a queued signal/slot argument. */
Q_DECLARE_METATYPE(aether::Parity)
