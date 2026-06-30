#include <QtTest>
#include "core/bus_protocol.h"
#include "gui/frametablemodel.h"

class BusTest : public QObject {
    Q_OBJECT
private slots:
    void testProtocolVersion() {
        BusProtocol protocol;
        QVERIFY(!protocol.getProtocolVersion().isEmpty());
    }

    void testQueryInterfaces() {
        auto list = BusProtocol::queryInterfaces();
        QVERIFY(!list.isEmpty());
        
        bool foundMock = false;
        for (const auto& item : list) {
            if (item.name == "Mock-CAN" && item.type == "Mock") {
                foundMock = true;
                break;
            }
        }
        QVERIFY(foundMock);
    }

    void testFrameTableModel() {
        FrameTableModel model;
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.columnCount(), 7);

        AetherFrame frame;
        frame.timestamp_us = 1000000; // 1s
        frame.bus_identifier = 1;
        frame.payload_id = 0x1A0;
        frame.length = 2;
        frame.data = {0x12, 0x34};

        model.addFrame(frame);
        QCOMPARE(model.rowCount(), 1);

        // Verify Data formatting
        QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toInt(), 0); // Index
        QCOMPARE(model.data(model.index(0, 1), Qt::DisplayRole).toString(), QString("1.000000")); // Time
        QCOMPARE(model.data(model.index(0, 3), Qt::DisplayRole).toString(), QString("0X1A0")); // Hex ID
        QCOMPARE(model.data(model.index(0, 5), Qt::DisplayRole).toInt(), 2); // Length
        QCOMPARE(model.data(model.index(0, 6), Qt::DisplayRole).toString(), QString("12 34")); // Bytes
    }
};

QTEST_MAIN(BusTest)
#include "bus_test.moc"
