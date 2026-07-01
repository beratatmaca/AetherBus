#include "bus_test.hpp"
#include "core/common/format_codec.hpp"
#include "core/common/stats_calculator.hpp"
#include "core/serial/serial_types.hpp"

using namespace aether;

void BusTest::initTestCase() {
    // Lifecycle setup if needed
}

void BusTest::cleanupTestCase() {
    // Lifecycle teardown if needed
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

QTEST_MAIN(BusTest)
