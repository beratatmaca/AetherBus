#pragma once
#include <QString>
#include <QByteArray>
#include <QStringList>
#include "core/serial/serial_types.hpp" // For Direction

namespace aether {

struct EthernetConfig {
    QString interfaceName;
    QString bpfFilter;
    bool promiscuous = false;
};

// Flags for visual construction
enum EthernetProto : uint8_t {
    ProtoUDP = 17,
    ProtoTCP = 6,
    ProtoICMP = 1
};

} // namespace aether
