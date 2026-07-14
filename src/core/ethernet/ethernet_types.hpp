#pragma once
#include <QString>
#include <QByteArray>
#include <QStringList>
#include "core/serial/serial_types.hpp" // For Direction

namespace aether {

/** @brief Parameters describing how to open a pcap capture on a network interface. */
struct EthernetConfig {
    QString interfaceName;      ///< Network interface name, e.g. @c eth0.
    QString bpfFilter;          ///< BPF capture filter expression; empty => capture everything.
    bool promiscuous = false;   ///< Open the interface in promiscuous mode.
};

/** @brief Flags for visual construction. */
enum EthernetProto : uint8_t {
    ProtoUDP = 17,
    ProtoTCP = 6,
    ProtoICMP = 1
};

} // namespace aether
