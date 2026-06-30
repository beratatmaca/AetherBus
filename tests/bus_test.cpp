// Unit + integration tests for the AetherBus interception backend.
//
//  - codec: pure byte<->text conversions and injection-field parsing.
//  - PtyProxy: direction tagging on injection, and real byte forwarding
//    through the poll() multiplexing loop using a PTY-backed fake device.
#include "core/format_codec.h"
#include "core/pty_proxy.h"
#include "core/serial_types.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <cstdlib>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

using namespace aether;

namespace {

// Open a raw PTY master/slave pair; returns the master fd and slave path.
bool makeRawPty(int &masterFd, QString &slavePath) {
    masterFd = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (masterFd < 0 || ::grantpt(masterFd) != 0 || ::unlockpt(masterFd) != 0) {
        return false;
    }
    const char *name = ::ptsname(masterFd);
    if (name == nullptr) {
        return false;
    }
    slavePath = QString::fromLocal8Bit(name);
    return true;
}

// Put a tty fd into raw mode so writes are not line-buffered.
void makeRaw(int fd) {
    termios tio{};
    ::tcgetattr(fd, &tio);
    ::cfmakeraw(&tio);
    ::tcsetattr(fd, TCSANOW, &tio);
}

// Read up to want bytes from fd, polling until timeout elapses.
QByteArray readWithTimeout(int fd, int want, int timeoutMs) {
    QByteArray out;
    QElapsedTimer timer;
    timer.start();
    while (out.size() < want && timer.elapsed() < timeoutMs) {
        pollfd pfd{fd, POLLIN, 0};
        const int rc = ::poll(&pfd, 1, 50);
        if (rc > 0 && (pfd.revents & POLLIN) != 0) {
            char buf[256];
            const ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, static_cast<int>(n));
            }
        }
    }
    return out;
}

}  // namespace

class BusTest : public QObject {
    Q_OBJECT

private slots:
    void hexFormatting();
    void asciiFormatting();
    void asciiEscapedFormatting();
    void binaryAndDecimalFormatting();
    void parseValidHex();
    void parseRejectsGarbage();
    void parseDecAndBin();

    void proxyStartsAndExposesSlave();
    void injectionTagsDirection();
    void loopbackForwardsBothDirections();
    void flowControlConfigOpens();
};

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

void BusTest::parseRejectsGarbage() {
    QByteArray out;
    int err = -1;
    QVERIFY(!codec::parseHexString(QStringLiteral("41 ZZ 43"), out, &err));
    QCOMPARE(err, 1);

    QVERIFY(!codec::parseHexString(QStringLiteral("123"), out));  // > 2 digits
}

void BusTest::proxyStartsAndExposesSlave() {
    int physMaster = -1;
    QString physSlave;
    QVERIFY(makeRawPty(physMaster, physSlave));

    PtyProxy proxy;
    SerialConfig cfg;
    cfg.device = physSlave;
    cfg.baud = 115200;
    QVERIFY(proxy.open(cfg));
    QVERIFY(proxy.isRunning());
    QVERIFY(!proxy.slavePath().isEmpty());

    proxy.close();
    QVERIFY(!proxy.isRunning());
    ::close(physMaster);
}

void BusTest::injectionTagsDirection() {
    int physMaster = -1;
    QString physSlave;
    QVERIFY(makeRawPty(physMaster, physSlave));

    PtyProxy proxy;
    SerialConfig cfg;
    cfg.device = physSlave;
    QVERIFY(proxy.open(cfg));

    // injection emits from the calling thread, so QSignalSpy is reliable here.
    QSignalSpy spy(&proxy, &PtyProxy::chunkCaptured);
    QVERIFY(spy.isValid());

    QVERIFY(proxy.injectToApp(QByteArray::fromHex("AABB")));
    QVERIFY(proxy.injectToDevice(QByteArray::fromHex("CCDD")));
    QCOMPARE(spy.count(), 2);

    const auto first = qvariant_cast<CapturedChunk>(spy.at(0).at(0));
    QCOMPARE(first.dir, Direction::Rx);  // toward the app
    QCOMPARE(first.data, QByteArray::fromHex("AABB"));

    const auto second = qvariant_cast<CapturedChunk>(spy.at(1).at(0));
    QCOMPARE(second.dir, Direction::Tx);  // toward the device
    QCOMPARE(second.data, QByteArray::fromHex("CCDD"));

    proxy.close();
    ::close(physMaster);
}

void BusTest::loopbackForwardsBothDirections() {
    int physMaster = -1;
    QString physSlave;
    QVERIFY(makeRawPty(physMaster, physSlave));
    makeRaw(physMaster);

    PtyProxy proxy;
    SerialConfig cfg;
    cfg.device = physSlave;
    QVERIFY(proxy.open(cfg));

    // Open the app side of the proxy's PTY in raw mode.
    const int appFd = ::open(proxy.slavePath().toLocal8Bit().constData(), O_RDWR | O_NOCTTY);
    QVERIFY(appFd >= 0);
    makeRaw(appFd);

    // Tx path: app -> proxy master -> physical UART -> physMaster reads it.
    const QByteArray tx = QByteArray::fromHex("4142");
    QCOMPARE(::write(appFd, tx.constData(), tx.size()), static_cast<ssize_t>(tx.size()));
    QCOMPARE(readWithTimeout(physMaster, tx.size(), 2000), tx);

    // Rx path: physMaster -> physical slave -> proxy -> app side reads it.
    const QByteArray rx = QByteArray::fromHex("5A5B");
    QCOMPARE(::write(physMaster, rx.constData(), rx.size()), static_cast<ssize_t>(rx.size()));
    QCOMPARE(readWithTimeout(appFd, rx.size(), 2000), rx);

    ::close(appFd);
    proxy.close();
    ::close(physMaster);
}

void BusTest::flowControlConfigOpens() {
    int physMaster = -1;
    QString physSlave;
    QVERIFY(makeRawPty(physMaster, physSlave));

    PtyProxy proxy;
    SerialConfig cfg;
    cfg.device = physSlave;
    cfg.baud = 115200;
    cfg.flow = FlowControl::RtsCts;  // exercises the CRTSCTS termios path
    QVERIFY(proxy.open(cfg));
    QVERIFY(proxy.isRunning());

    proxy.close();
    ::close(physMaster);
}

QTEST_MAIN(BusTest)
#include "bus_test.moc"
