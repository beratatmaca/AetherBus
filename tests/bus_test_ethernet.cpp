#include "bus_test.hpp"
#include "core/ethernet/ethernet_backend.hpp"
#include "gui/panels/packet_constructor_panel.hpp"
#include "gui/sessions/ethernet_packet_model.hpp"
#include "gui/sessions/ethernet_session_widget.hpp"

#include <QApplication>
#include <QComboBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest/QtTest>

using namespace aether;

namespace {

/// warnIfTcpUnsupported()/onSendClicked() with an invalid MAC, and
/// saveCurrentAsMacro() all show a blocking modal dialog. Close whatever
/// modal is active shortly after it's expected to appear so exec() returns
/// and the test doesn't hang; optionally fill in a QInputDialog's text first.
void dismissNextModal(const QString &inputDialogText = QString()) {
    QTimer::singleShot(50, [inputDialogText] {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) {
            return;
        }
        if (auto *input = qobject_cast<QInputDialog *>(modal); input && !inputDialogText.isEmpty()) {
            input->setTextValue(inputDialogText);
            input->accept();
        } else {
            modal->close();
        }
    });
}

}  // namespace

void BusTest::ethernetBackendAndParsing() {
    // 1. Verify interfaces listing (libpcap works)
    QStringList interfaces = EthernetBackend::listInterfaces();
    qDebug() << "Found interfaces:" << interfaces;

    // 2. Test Packet Constructor Panel building a raw packet
    PacketConstructorPanel constructor;
    QSignalSpy spy(&constructor, &PacketConstructorPanel::packetReady);
    QVERIFY(spy.isValid());

    // Trigger packet construction
    QMetaObject::invokeMethod(&constructor, "onSendClicked");

    QCOMPARE(spy.count(), 1);
    QByteArray pkt = spy.first().at(0).toByteArray();

    // Verify packet header lengths (Ethernet II (14) + IP (20) + UDP (8) + payload (4) = 46 bytes)
    QCOMPARE(pkt.size(), 46);

    // Verify Ethernet header fields
    QCOMPARE(pkt.mid(0, 6), QByteArray::fromHex("FFFFFFFFFFFF"));  // Dest MAC
    QCOMPARE(pkt.mid(6, 6), QByteArray::fromHex("001122334455"));  // Src MAC
    QCOMPARE(pkt.mid(12, 2), QByteArray::fromHex("0800"));         // EtherType (IPv4)

    // Verify IP protocol (UDP = 17)
    QCOMPARE(static_cast<uint8_t>(pkt[23]), 17);
}

void BusTest::ethernetPacketConstructorIcmp() {
    PacketConstructorPanel constructor;
    auto *protoBox = constructor.findChild<QComboBox *>(QStringLiteral("protocolCombo"));
    QVERIFY(protoBox != nullptr);
    protoBox->setCurrentIndex(protoBox->findData(1));  // ICMP (1)

    QSignalSpy spy(&constructor, &PacketConstructorPanel::packetReady);
    QMetaObject::invokeMethod(&constructor, "onSendClicked");
    QCOMPARE(spy.count(), 1);

    const QByteArray pkt = spy.first().at(0).toByteArray();
    QCOMPARE(static_cast<uint8_t>(pkt[23]), 1);  // IP protocol byte = ICMP

    // Ethernet(14) + IP(20) + ICMP header(8) + payload ("DE AD BE EF" -> 4 bytes)
    QCOMPARE(pkt.size(), 14 + 20 + 8 + 4);

    const QByteArray icmp = pkt.mid(34, 8 + 4);
    QCOMPARE(static_cast<uint8_t>(icmp[0]), 8);  // Type: Echo Request
    QCOMPARE(static_cast<uint8_t>(icmp[1]), 0);  // Code

    // The ICMP checksum must make the whole message sum to 0 under the
    // Internet checksum algorithm (RFC 1071's standard verification test).
    uint32_t sum = 0;
    for (int i = 0; i + 1 < icmp.size(); i += 2) {
        sum += (static_cast<uint8_t>(icmp[i]) << 8) | static_cast<uint8_t>(icmp[i + 1]);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    QCOMPARE(static_cast<uint16_t>(~sum), static_cast<uint16_t>(0));
}

void BusTest::ethernetPacketConstructorTcp() {
    PacketConstructorPanel constructor;
    auto *protoBox = constructor.findChild<QComboBox *>(QStringLiteral("protocolCombo"));
    QVERIFY(protoBox != nullptr);
    protoBox->setCurrentIndex(protoBox->findData(6));  // TCP (6)

    QSignalSpy spy(&constructor, &PacketConstructorPanel::packetReady);

    QMetaObject::invokeMethod(&constructor, "onSendClicked");
    QCOMPARE(spy.count(), 1);

    const QByteArray pkt = spy.first().at(0).toByteArray();
    QCOMPARE(static_cast<uint8_t>(pkt[23]), 6);  // IP protocol byte claims TCP
    QCOMPARE(pkt.size(), 14 + 20 + 20 + 4);      // Eth (14) + IP (20) + TCP (20) + Payload (4)

    // Verify TCP source and destination ports
    uint16_t srcPort = (static_cast<uint8_t>(pkt[34]) << 8) | static_cast<uint8_t>(pkt[35]);
    uint16_t destPort = (static_cast<uint8_t>(pkt[36]) << 8) | static_cast<uint8_t>(pkt[37]);
    QCOMPARE(srcPort, 1234);
    QCOMPARE(destPort, 9999);
}

void BusTest::ethernetPacketConstructorInvalidMacBlocksSend() {
    PacketConstructorPanel constructor;
    auto *destMacEdit = constructor.findChild<QLineEdit *>(QStringLiteral("destMacEdit"));
    QVERIFY(destMacEdit != nullptr);
    destMacEdit->setText(QStringLiteral("not-a-mac"));

    QSignalSpy spy(&constructor, &PacketConstructorPanel::packetReady);
    dismissNextModal();
    QMetaObject::invokeMethod(&constructor, "onSendClicked");

    // Invalid MAC blocks the send entirely rather than silently zeroing it.
    QCOMPARE(spy.count(), 0);
}

void BusTest::ethernetPacketConstructorMacroRoundTrip() {
    // Isolate from any macros a previous test run may have left behind.
    QSettings settings;
    settings.remove(QStringLiteral("ethernet_macros"));

    auto *constructor = new PacketConstructorPanel();
    QSignalSpy spy(constructor, &PacketConstructorPanel::packetReady);

    // Build the default UDP packet once to know what the macro should replay.
    QMetaObject::invokeMethod(constructor, "onSendClicked");
    QCOMPARE(spy.count(), 1);
    const QByteArray expectedPacket = spy.first().at(0).toByteArray();
    spy.clear();

    auto *saveMacroBtn = constructor->findChild<QPushButton *>(QStringLiteral("saveMacroButton"));
    QVERIFY(saveMacroBtn != nullptr);

    dismissNextModal(QStringLiteral("TestMacro"));
    saveMacroBtn->click();

    auto *macroBtn = constructor->findChild<QPushButton *>(QStringLiteral("TestMacro"));
    QVERIFY(macroBtn != nullptr);
    macroBtn->click();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toByteArray(), expectedPacket);

    // A fresh panel instance reloads the same macro from QSettings.
    delete constructor;
    auto *reloaded = new PacketConstructorPanel();
    auto *reloadedBtn = reloaded->findChild<QPushButton *>(QStringLiteral("TestMacro"));
    QVERIFY(reloadedBtn != nullptr);
    delete reloaded;

    settings.remove(QStringLiteral("ethernet_macros"));
}

void BusTest::ethernetPacketModelBasics() {
    EthernetPacketModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), static_cast<int>(EthernetPacketModel::ColCount));

    CapturedChunk rx;
    rx.timestampMs = 1000;
    rx.dir = Direction::Rx;
    rx.data = QByteArray::fromHex(
        "AABBCCDDEEFF11223344556608004500001C000000004011"
        "0000C0A80101C0A80102");  // Ethernet+IPv4(UDP) header
    model.appendPacket(rx);

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex dirIdx = model.index(0, EthernetPacketModel::ColDir);
    QCOMPARE(model.data(dirIdx, Qt::DisplayRole).toString(), QStringLiteral("Rx"));
    QCOMPARE(model.data(dirIdx, Qt::ForegroundRole).value<QColor>(), QColor(0x00, 0xbc, 0xd4));

    const QModelIndex srcIdx = model.index(0, EthernetPacketModel::ColSource);
    QCOMPARE(model.data(srcIdx, Qt::DisplayRole).toString(), QStringLiteral("192.168.1.1"));

    const QModelIndex protoIdx = model.index(0, EthernetPacketModel::ColProtocol);
    QCOMPARE(model.data(protoIdx, Qt::DisplayRole).toString(), QStringLiteral("UDP"));

    QCOMPARE(model.chunkAt(0).data, rx.data);

    model.clearPackets();
    QCOMPARE(model.rowCount(), 0);
}

void BusTest::ethernetPacketModelEvictsOldest() {
    EthernetPacketModel model;

    CapturedChunk first;
    first.timestampMs = 1;
    first.data = QByteArrayLiteral("first");
    model.appendPacket(first);

    for (int i = 0; i < EthernetPacketModel::kMaxRows; ++i) {
        CapturedChunk chunk;
        chunk.timestampMs = i + 2;
        chunk.data = QByteArray::number(i);
        model.appendPacket(chunk);
    }

    // kMaxRows + 1 packets were appended; the very first one must have been
    // evicted, and the row count must stay capped rather than growing unbounded.
    QCOMPARE(model.rowCount(), EthernetPacketModel::kMaxRows);
    QVERIFY(model.chunkAt(0).data != QByteArrayLiteral("first"));
}

void BusTest::ethernetPacketModelBatchClampsAndEvicts() {
    EthernetPacketModel model;

    // Seed with a few rows, then append a single batch larger than the cap.
    for (int i = 0; i < 5; ++i) {
        CapturedChunk seed;
        seed.timestampMs = i;
        seed.data = QByteArrayLiteral("seed") + QByteArray::number(i);
        model.appendPacket(seed);
    }

    QVector<CapturedChunk> batch;
    const int overCap = EthernetPacketModel::kMaxRows + 250;
    batch.reserve(overCap);
    for (int i = 0; i < overCap; ++i) {
        CapturedChunk chunk;
        chunk.timestampMs = 1000 + i;
        chunk.data = QByteArray::number(i);
        batch.append(chunk);
    }
    model.appendPackets(batch);

    // The count is clamped to the cap, the seed rows are gone, and only the
    // newest kMaxRows of the oversized batch survive in order (so the last row
    // is the batch's final element and the first is batch element #250).
    QCOMPARE(model.rowCount(), EthernetPacketModel::kMaxRows);
    QCOMPARE(model.chunkAt(model.rowCount() - 1).data, QByteArray::number(overCap - 1));
    QCOMPARE(model.chunkAt(0).data, QByteArray::number(overCap - EthernetPacketModel::kMaxRows));
}

void BusTest::ethernetBackendOpenInvalidInterfaceFails() {
    EthernetBackend backend;
    QSignalSpy errorSpy(&backend, &EthernetBackend::errorOccurred);
    QVERIFY(errorSpy.isValid());

    EthernetConfig cfg;
    cfg.interfaceName = QStringLiteral("aetherbus-test-does-not-exist");

    QVERIFY(!backend.open(cfg));
    QVERIFY(!backend.isRunning());
    QCOMPARE(errorSpy.count(), 1);
}

void BusTest::ethernetSessionSettingsRoundTrip() {
    QSettings in;
    in.beginGroup(QStringLiteral("test_ethernet_in"));
    in.setValue(QStringLiteral("interface"), QStringLiteral("eth-test0"));
    in.setValue(QStringLiteral("bpfFilter"), QStringLiteral("udp port 5555"));
    in.endGroup();

    EthernetSessionWidget widget;
    in.beginGroup(QStringLiteral("test_ethernet_in"));
    widget.loadSettings(in);
    in.endGroup();

    QSettings out;
    out.beginGroup(QStringLiteral("test_ethernet_out"));
    widget.saveSettings(out);
    out.endGroup();

    out.beginGroup(QStringLiteral("test_ethernet_out"));
    QCOMPARE(out.value(QStringLiteral("interface")).toString(), QStringLiteral("eth-test0"));
    QCOMPARE(out.value(QStringLiteral("bpfFilter")).toString(), QStringLiteral("udp port 5555"));
    out.endGroup();
}
