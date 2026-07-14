#pragma once
#include <QString>

namespace aether {

/**
 * @brief Parameters describing how to capture USB packets.
 */
struct UsbConfig {
    QString interfaceName;  ///< Device interface name, e.g., usbmon0 or USBPcap1.
};

}  // namespace aether
