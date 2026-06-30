#include <QtTest>
#include "core/bus_protocol.h"

class BusTest : public QObject {
    Q_OBJECT
private slots:
    void testProtocolVersion() {
        BusProtocol protocol;
        QVERIFY(!protocol.getProtocolVersion().isEmpty());
    }
};

QTEST_MAIN(BusTest)
#include "bus_test.moc"
