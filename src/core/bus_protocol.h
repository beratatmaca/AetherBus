#pragma once

#include <QObject>
#include <QString>

#include <QList>

#include <vector>

struct AetherFrame {
    uint64_t timestamp_us;     // Microsecond-accurate kernel time
    uint32_t bus_identifier;   // Globally assigned tracking ID
    uint32_t payload_id;       // CAN ID, EtherType, or Serial Header
    uint16_t length;           // Absolute payload size
    std::vector<uint8_t> data; // Raw frame contents
};

struct BusInterfaceInfo {
    QString name;
    QString type;        // "CAN", "Serial", "Ethernet", "Mock"
    QString description;
};

class BusProtocol : public QObject {
    Q_OBJECT
public:
    explicit BusProtocol(QObject *parent = nullptr);
    QString getProtocolVersion() const;
    static QList<BusInterfaceInfo> queryInterfaces();
};

#include <QTimer>

class SimulatorWorker : public QObject {
    Q_OBJECT
public:
    explicit SimulatorWorker(QObject *parent = nullptr);
    void start();
    void stop();

signals:
    void frameReady(const AetherFrame& frame);

private slots:
    void generateFrame();

private:
    QTimer* m_timer;
    uint32_t m_cycleCounter;
};
