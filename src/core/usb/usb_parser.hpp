#pragma once
#include "core/serial/serial_types.hpp"
#include <QByteArray>
#include <QString>

namespace aether {

/**
 * @brief USB transfer types mapping to usbmon specification.
 */
enum class UsbTransferType : uint8_t {
    Isochronous = 0,
    Interrupt = 1,
    Control = 2,
    Bulk = 3,
    Unknown = 255
};

/**
 * @brief URB event types from the host controller.
 */
enum class UsbEventType : uint8_t {
    Submit = 'S',
    Complete = 'C',
    Error = 'E',
    Unknown = 255
};

/**
 * @brief Parsed USB Request Block (URB) transaction details.
 */
struct UsbUrbInfo {
    quint64 id = 0;                                         ///< URB ID.
    quint16 busId = 0;                                      ///< USB Bus ID.
    quint8 deviceAddress = 0;                               ///< USB Device address on the bus.
    quint8 endpoint = 0;                                    ///< Endpoint address.
    Direction direction = Direction::Rx;                    ///< Rx = IN (Device to Host), Tx = OUT (Host to Device).
    UsbTransferType transferType = UsbTransferType::Unknown; ///< USB Transfer type.
    UsbEventType eventType = UsbEventType::Unknown;         ///< URB Event type.
    qint32 status = 0;                                      ///< URB completion status (0 = success).
    quint32 length = 0;                                     ///< Total transfer length.
    QByteArray setupPacket;                                 ///< 8-byte setup packet if Control transfer.
    QByteArray payload;                                     ///< Captured raw USB transfer payload.
    QString infoText;                                       ///< Human-readable transaction brief.
    bool isValid = false;                                   ///< Flag indicating if the packet header was parsed successfully.
};

class UsbParser {
public:
    /**
     * @brief Parse a raw PCAP USB packet (containing usbmon header) into UsbUrbInfo.
     * @param packet Raw captured packet buffer.
     * @return Decoded UsbUrbInfo structure.
     */
    static UsbUrbInfo parseUrb(const QByteArray &packet);

    /**
     * @brief Decode standard USB descriptor bytes into a human-readable summary.
     * @param descType Descriptor type (e.g., 1 for Device, 2 for Config).
     * @param data Descriptor bytes.
     * @return Multi-line formatted description.
     */
    static QString decodeDescriptor(quint8 descType, const QByteArray &data);
};

}  // namespace aether
