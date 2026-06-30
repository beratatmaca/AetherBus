#include "bus_protocol.h"
#include "aether/version.h"

BusProtocol::BusProtocol(QObject *parent) : QObject(parent) {}

QString BusProtocol::getProtocolVersion() const {
    return QString(AETHER_VERSION_STRING);
}
