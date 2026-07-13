#include "bus_test.hpp"
#include "core/common/format_codec.hpp"
#include "core/common/stats_calculator.hpp"
#include "core/serial/serial_types.hpp"
#include "core/ethernet/ethernet_pcap.hpp"

#include <QCoreApplication>
#include <QDataStream>
#include <QSettings>
#include <QTemporaryFile>

using namespace aether;

namespace {

/// Build a minimal classic (non-pcapng) pcap byte blob: a 24-byte global
/// header (magic 0xa1b2c3d4, given @p linkType) followed by one 16-byte
/// record header + raw bytes per entry in @p frames.
QByteArray buildClassicPcap(const QVector<QByteArray> &frames, quint32 linkType = 1) {
    QByteArray blob;
    QDataStream out(&blob, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out << static_cast<quint32>(0xa1b2c3d4);
    out << static_cast<quint16>(2);
    out << static_cast<quint16>(4);
    out << static_cast<qint32>(0);
    out << static_cast<quint32>(0);
    out << static_cast<quint32>(65535);
    out << linkType;
    for (const QByteArray &frame : frames) {
        out << static_cast<quint32>(5);       // ts_sec
        out << static_cast<quint32>(500000);  // ts_usec
        out << static_cast<quint32>(frame.size());
        out << static_cast<quint32>(frame.size());
        out.writeRawData(frame.constData(), static_cast<int>(frame.size()));
    }
    return blob;
}

}  // namespace

void BusTest::initTestCase() {
    // QTEST_MAIN's generated main() never calls setOrganizationName/setApplicationName
    // (unlike the real app's main.cpp), and every panel/session/MainWindow reads
    // and writes QSettings via the no-arg constructor. Without this, tests would
    // resolve to a shared fallback location — either colliding with the real
    // app's own ~/.config store, or with each other across test runs. Force
    // IniFormat (rather than NativeFormat, which is the Windows registry) and
    // redirect it into a throwaway temp directory for the whole test binary.
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("AetherBus Project"));
    QCoreApplication::setApplicationName(QStringLiteral("AetherBus"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());

    // Several tests construct a MainWindow, which schedules a 100ms singleShot
    // to show the Welcome Tutorial (a blocking modal exec()) if this is unset.
    // Suite-wide default off; the one test that exercises the tutorial itself
    // sets its own value first.
    QSettings appSettings;
    appSettings.setValue(QStringLiteral("ui/show_tutorial"), false);
}

void BusTest::cleanupTestCase() {
    // m_settingsDir cleans up its own temp directory on destruction.
}

void BusTest::hexFormatting() {
    const QByteArray bytes = QByteArray::fromHex("41420D0A");
    QCOMPARE(codec::toHex(bytes), QStringLiteral("41 42 0D 0A"));
    QCOMPARE(codec::toHex(QByteArray()), QString());
}

void BusTest::asciiFormatting() {
    QByteArray bytes;
    bytes.append('A');
    bytes.append('B');
    bytes.append(static_cast<char>(0x0D));
    bytes.append(static_cast<char>(0x00));
    QCOMPARE(codec::toAscii(bytes), QStringLiteral("AB.."));
}

void BusTest::asciiEscapedFormatting() {
    QByteArray bytes;
    bytes.append('A');
    bytes.append(static_cast<char>(0x0D));
    bytes.append(static_cast<char>(0x0A));
    bytes.append(static_cast<char>(0x09));
    bytes.append(static_cast<char>(0x01));
    QCOMPARE(codec::toAsciiEscaped(bytes), QStringLiteral("A\\r\\n\\t\\x01"));
}

void BusTest::binaryAndDecimalFormatting() {
    const QByteArray bytes = QByteArray::fromHex("4142");
    QCOMPARE(codec::toBinary(bytes), QStringLiteral("01000001 01000010"));
    QCOMPARE(codec::toDecimal(bytes), QStringLiteral("065 066"));
}

void BusTest::parseValidHex() {
    QByteArray out;
    int err = -1;
    QVERIFY(codec::parseHexString(QStringLiteral("41 42 0d 0A"), out, &err));
    QCOMPARE(out, QByteArray::fromHex("41420D0A"));
    QCOMPARE(err, -1);

    // Single-digit tokens and irregular whitespace are tolerated.
    QVERIFY(codec::parseHexString(QStringLiteral("  7   ff "), out));
    QCOMPARE(out, QByteArray::fromHex("07FF"));
}

void BusTest::parseDecAndBin() {
    QByteArray out;
    int err = -1;

    QVERIFY(codec::parseDecString(QStringLiteral("65 66 13 10"), out, &err));
    QCOMPARE(out, QByteArray::fromHex("41420D0A"));
    QVERIFY(!codec::parseDecString(QStringLiteral("65 256"), out, &err));  // out of range
    QCOMPARE(err, 1);

    QVERIFY(codec::parseBinString(QStringLiteral("01000001 1010"), out, &err));
    QCOMPARE(out, QByteArray::fromHex("410A"));
    QVERIFY(!codec::parseBinString(QStringLiteral("01000001 2"), out, &err));  // not binary
    QCOMPARE(err, 1);
}

void BusTest::encodePayloadFormatsAndEndings() {
    QByteArray out;
    QString err;

    // HEX payload, no ending.
    QVERIFY(codec::encodePayload(static_cast<int>(codec::PayloadFormat::Hex), QStringLiteral("41 42"),
                                 static_cast<int>(codec::LineEnding::None), out, &err));
    QCOMPARE(out, QByteArray::fromHex("4142"));

    // ASCII payload with CR+LF ending appended.
    QVERIFY(codec::encodePayload(static_cast<int>(codec::PayloadFormat::Ascii), QStringLiteral("AB"),
                                 static_cast<int>(codec::LineEnding::CRLF), out, &err));
    QCOMPARE(out, QByteArray::fromHex("41420D0A"));

    // DEC payload with a lone CR.
    QVERIFY(codec::encodePayload(static_cast<int>(codec::PayloadFormat::Dec), QStringLiteral("65 66"),
                                 static_cast<int>(codec::LineEnding::CR), out, &err));
    QCOMPARE(out, QByteArray::fromHex("41420D"));

    // Malformed input is rejected with a reason and leaves out untouched.
    QVERIFY(!codec::encodePayload(static_cast<int>(codec::PayloadFormat::Hex), QStringLiteral("ZZ"),
                                  static_cast<int>(codec::LineEnding::None), out, &err));
    QVERIFY(!err.isEmpty());
}

void BusTest::parseRejectsGarbage() {
    QByteArray out;
    int err = -1;
    QVERIFY(!codec::parseHexString(QStringLiteral("41 ZZ 43"), out, &err));
    QCOMPARE(err, 1);

    QVERIFY(!codec::parseHexString(QStringLiteral("123"), out));  // > 2 digits
}

void BusTest::statsCalculatorBasics() {
    StatsCalculator calc;
    calc.setSerialConfig(9600, 8, Parity::None, 1);

    // Initial state
    QCOMPARE(calc.rxBytes(), static_cast<qint64>(0));
    QCOMPARE(calc.txBytes(), static_cast<qint64>(0));

    // Feed Rx chunk
    CapturedChunk rx1;
    rx1.dir = Direction::Rx;
    rx1.timestampMs = 1000;
    rx1.data = "hello";
    calc.addChunk(rx1);

    QCOMPARE(calc.rxBytes(), static_cast<qint64>(5));
    QCOMPARE(calc.rxGap().count, static_cast<qint64>(0));  // Only 1 packet, no gap

    // Feed Tx chunk
    CapturedChunk tx1;
    tx1.dir = Direction::Tx;
    tx1.timestampMs = 1050;
    tx1.data = "world!!";
    calc.addChunk(tx1);

    QCOMPARE(calc.txBytes(), static_cast<qint64>(7));

    // Feed second Rx chunk
    CapturedChunk rx2;
    rx2.dir = Direction::Rx;
    rx2.timestampMs = 1200;
    rx2.data = "!";
    calc.addChunk(rx2);

    QCOMPARE(calc.rxBytes(), static_cast<qint64>(6));

    // Rx -> Rx gap should be updated: 1200 - 1000 = 200ms
    QCOMPARE(calc.rxGap().count, static_cast<qint64>(1));
    QCOMPARE(calc.rxGap().min, static_cast<qint64>(200));

    // Tx -> Rx latency should be updated: 1200 - 1050 = 150ms
    QCOMPARE(calc.txRxGap().count, static_cast<qint64>(1));
    QCOMPARE(calc.txRxGap().min, static_cast<qint64>(150));

    // Test rates rolling & utilization
    calc.rollRates();
    QCOMPARE(calc.currentRxRate(), static_cast<double>(6));
    QCOMPARE(calc.currentTxRate(), static_cast<double>(7));

    // Baud: 9600 bps. bitsPerChar: 10. Max bytes/sec = 960
    // Rx utilization: 6 / 960 * 100 = 0.625 %
    // Tx utilization: 7 / 960 * 100 = 0.729 %
    QCOMPARE(calc.rxBaudUtilization(), 0.625);
}

void BusTest::ethernetPcapRoundTrip() {
    const QByteArray frame1 = QByteArray::fromHex("AABBCCDDEEFF112233445566080045000014");
    const QByteArray frame2 = QByteArray::fromHex("112233445566AABBCCDDEEFF0806000108000604");

    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(buildClassicPcap({frame1, frame2}));
    file.close();

    QString error;
    auto parsed = readEthernetPcap(file.fileName(), &error);
    QVERIFY2(parsed.has_value(), qPrintable(error));
    if (!parsed.has_value()) {
        return;  // explicit guard so static analysis sees the optional is checked
    }
    QCOMPARE(parsed->size(), 2);

    QCOMPARE((*parsed)[0].data, frame1);
    QCOMPARE((*parsed)[0].dir, Direction::Tx);
    QCOMPARE((*parsed)[0].timestampMs, static_cast<qint64>(5 * 1000 + 500000 / 1000));
    QCOMPARE((*parsed)[1].data, frame2);
}

void BusTest::ethernetPcapRejectsBadMagic() {
    QByteArray blob = buildClassicPcap({QByteArray::fromHex("AABBCCDDEEFF")});
    blob[0] = static_cast<char>(0x00);  // corrupt the magic number

    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(blob);
    file.close();

    QString error;
    auto parsed = readEthernetPcap(file.fileName(), &error);
    QVERIFY(!parsed.has_value());
    QVERIFY(!error.isEmpty());
}

void BusTest::ethernetPcapRejectsWrongLinkType() {
    QByteArray blob = buildClassicPcap({QByteArray::fromHex("AABBCCDDEEFF")}, /*linkType=*/101);  // LINKTYPE_RAW

    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(blob);
    file.close();

    QString error;
    auto parsed = readEthernetPcap(file.fileName(), &error);
    QVERIFY(!parsed.has_value());
    QVERIFY(error.contains(QStringLiteral("link type")));
}

void BusTest::ethernetPcapRejectsTruncatedRecord() {
    QByteArray blob = buildClassicPcap({QByteArray::fromHex("AABBCCDDEEFF1122334455660800")});
    blob.chop(4);  // truncate mid-payload; the record header still claims the full length

    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(blob);
    file.close();

    QString error;
    auto parsed = readEthernetPcap(file.fileName(), &error);
    QVERIFY(!parsed.has_value());
    QVERIFY(error.contains(QStringLiteral("Truncated")));
}

void BusTest::ethernetPcapWriterRoundTrip() {
    const QByteArray frame1 = QByteArray::fromHex("AABBCCDDEEFF112233445566080045000014");
    const QByteArray frame2 = QByteArray::fromHex("112233445566AABBCCDDEEFF0806000108000604");

    QTemporaryFile file;
    QVERIFY(file.open());
    const QString path = file.fileName();
    file.close();

    EthernetPcapWriter writer;
    QVERIFY(!writer.isOpen());
    QString error;
    QVERIFY2(writer.open(path, &error), qPrintable(error));
    QVERIFY(writer.isOpen());

    writer.writePacket(5500, frame1);
    writer.writePacket(6000, frame2);
    writer.close();
    QVERIFY(!writer.isOpen());

    auto parsed = readEthernetPcap(path, &error);
    QVERIFY2(parsed.has_value(), qPrintable(error));
    if (!parsed.has_value()) {
        return;  // explicit guard so static analysis sees the optional is checked
    }
    QCOMPARE(parsed->size(), 2);
    QCOMPARE((*parsed)[0].data, frame1);
    QCOMPARE((*parsed)[0].timestampMs, static_cast<qint64>(5500));
    QCOMPARE((*parsed)[1].data, frame2);
    QCOMPARE((*parsed)[1].timestampMs, static_cast<qint64>(6000));
}

QTEST_MAIN(BusTest)
