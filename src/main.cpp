#include <QCoreApplication>
#include <QDebug>
#include "core/bus_protocol.h"

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);
    BusProtocol protocol;
    qDebug() << "AetherBus version:" << protocol.getProtocolVersion();
    return 0;
}
