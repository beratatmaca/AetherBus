#include "bus_protocol.h"
#include "aether/version.h"

#ifdef HAVE_QT_SERIALPORT
#include <QSerialPortInfo>
#endif
#include <QNetworkInterface>

BusProtocol::BusProtocol(QObject *parent) : QObject(parent) {}

QString BusProtocol::getProtocolVersion() const {
    return QString(AETHER_VERSION_STRING);
}

QList<BusInterfaceInfo> BusProtocol::queryInterfaces() {
    QList<BusInterfaceInfo> list;

    // 1. Scan Serial Ports
#ifdef HAVE_QT_SERIALPORT
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        BusInterfaceInfo item;
        item.name = info.portName();
        item.type = "Serial";
        item.description = info.description().isEmpty() ? "Serial Port Device" : info.description();
        list.append(item);
    }
#endif

    // 2. Scan Network Interfaces (for Ethernet & SocketCAN)
    for (const QNetworkInterface &net : QNetworkInterface::allInterfaces()) {
        // Skip loopback interfaces
        if (net.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }

        BusInterfaceInfo item;
        item.name = net.name();
        
        // Simple heuristic for Linux: SocketCAN interfaces often start with can or vcan
        if (net.name().startsWith("can") || net.name().startsWith("vcan")) {
            item.type = "CAN";
            item.description = "SocketCAN Network Interface";
        } else {
            item.type = "Ethernet";
            item.description = net.humanReadableName() + " (Layer 2 Ethernet)";
        }
        list.append(item);
    }

    // 3. Add Mock Interfaces for cross-platform simulation and testing
    list.append(BusInterfaceInfo{"Mock-CAN", "Mock", "Simulated CAN Bus (Continuous test frames)"});
    list.append(BusInterfaceInfo{"Mock-Serial", "Mock", "Simulated UART Serial Port (Loopback test)"});
    list.append(BusInterfaceInfo{"Mock-Ethernet", "Mock", "Simulated Layer 2 Ethernet Network"});

    return list;
}

#include <QDateTime>

SimulatorWorker::SimulatorWorker(QObject *parent)
    : QObject(parent), m_cycleCounter(0) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &SimulatorWorker::generateFrame);
}

void SimulatorWorker::start() {
    m_timer->start(100); // 100ms interval
}

void SimulatorWorker::stop() {
    m_timer->stop();
}

void SimulatorWorker::generateFrame() {
    m_cycleCounter++;

    AetherFrame frame;
    frame.timestamp_us = QDateTime::currentMSecsSinceEpoch() * 1000;
    frame.bus_identifier = 1; // Simulated Interface Bus 1

    if (m_cycleCounter % 2 == 0) {
        // Engine RPM simulated ID (0x1A0)
        frame.payload_id = 0x1A0;
        frame.length = 8;
        
        // Simulating varying engine speed (between 1000 and 6000 RPM)
        uint16_t rpm = 1000 + (m_cycleCounter * 20) % 5000;
        frame.data.resize(8, 0);
        // Pack Big Endian RPM into bytes 0-1
        frame.data[0] = static_cast<uint8_t>((rpm >> 8) & 0xFF);
        frame.data[1] = static_cast<uint8_t>(rpm & 0xFF);
    } else {
        // Coolant Temp simulated ID (0x1F0)
        frame.payload_id = 0x1F0;
        frame.length = 8;

        // Simulating coolant temp rising to 95 degrees C
        uint8_t temp = 60 + (m_cycleCounter / 3) % 36;
        frame.data.resize(8, 0);
        frame.data[0] = temp;
    }

    emit frameReady(frame);
}
