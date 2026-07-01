// Unit + integration tests for the AetherBus interception backend.
//
//  - codec: pure byte<->text conversions and injection-field parsing.
//  - PtyProxy: direction tagging on injection, and real byte forwarding
//    through the poll() multiplexing loop using a PTY-backed fake device.
#include "core/format_codec.h"
#include "core/pty_proxy.h"
#include "core/serial_types.h"
#include "core/stats_calculator.h"
#include "gui/consoleview.h"
#include "gui/macrobar.h"
#include "gui/theme_controller.h"
#include "gui/mainwindow.h"
#include <QComboBox>
#include <QTabWidget>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QtTest/QtTest>

#include <cerrno>
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

    void statsCountForwardedBytes();
    void mirrorsSlaveBaudToDevice();
    void pcapCaptureWritesRecords();
    void writeQueueDropsWhenPeerStalls();
    void statsCalculatorBasics();
    void guiConsoleView();
    void guiMacroBar();
    void guiThemeController();
    void guiMainWindow();
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

void BusTest::statsCountForwardedBytes() {
    int physMaster = -1;
    QString physSlave;
    QVERIFY(makeRawPty(physMaster, physSlave));
    makeRaw(physMaster);

    PtyProxy proxy;
    SerialConfig cfg;
    cfg.device = physSlave;
    QVERIFY(proxy.open(cfg));

    const int appFd = ::open(proxy.slavePath().toLocal8Bit().constData(), O_RDWR | O_NOCTTY);
    QVERIFY(appFd >= 0);
    makeRaw(appFd);

    // app -> device (Tx) and device -> app (Rx), two bytes each.
    const QByteArray tx = QByteArray::fromHex("4142");
    QCOMPARE(::write(appFd, tx.constData(), tx.size()), static_cast<ssize_t>(tx.size()));
    QCOMPARE(readWithTimeout(physMaster, tx.size(), 2000), tx);
    const QByteArray rx = QByteArray::fromHex("5A5B");
    QCOMPARE(::write(physMaster, rx.constData(), rx.size()), static_cast<ssize_t>(rx.size()));
    QCOMPARE(readWithTimeout(appFd, rx.size(), 2000), rx);

    // The backend counts wire bytes independently of the (absent) GUI.
    QTRY_COMPARE(proxy.stats().tx, static_cast<quint64>(tx.size()));
    QTRY_COMPARE(proxy.stats().rx, static_cast<quint64>(rx.size()));
    QCOMPARE(proxy.stats().dropped, static_cast<quint64>(0));

    ::close(appFd);
    proxy.close();
    ::close(physMaster);
}

void BusTest::mirrorsSlaveBaudToDevice() {
    int physMaster = -1;
    QString physSlave;
    QVERIFY(makeRawPty(physMaster, physSlave));
    makeRaw(physMaster);

    PtyProxy proxy;
    SerialConfig cfg;
    cfg.device = physSlave;
    cfg.baud = 115200;
    QVERIFY(proxy.open(cfg));

    QSignalSpy spy(&proxy, &PtyProxy::lineReconfigured);
    QVERIFY(spy.isValid());

    const int appFd = ::open(proxy.slavePath().toLocal8Bit().constData(), O_RDWR | O_NOCTTY);
    QVERIFY(appFd >= 0);

    // The target application reconfigures the slave line to 9600 8N1.
    termios tio{};
    QCOMPARE(::tcgetattr(appFd, &tio), 0);
    ::cfmakeraw(&tio);
    ::cfsetispeed(&tio, B9600);
    ::cfsetospeed(&tio, B9600);
    QCOMPARE(::tcsetattr(appFd, TCSANOW, &tio), 0);

    // The backend catches the change (TIOCPKT_IOCTL) and reports the new params.
    QVERIFY(spy.wait(2000));
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 9600);

    // ...and mirrors it onto the physical UART: its PTY peer now reads 9600.
    termios phys{};
    QCOMPARE(::tcgetattr(physMaster, &phys), 0);
    QCOMPARE(::cfgetospeed(&phys), static_cast<speed_t>(B9600));

    ::close(appFd);
    proxy.close();
    ::close(physMaster);
}

void BusTest::pcapCaptureWritesRecords() {
    int physMaster = -1;
    QString physSlave;
    QVERIFY(makeRawPty(physMaster, physSlave));
    makeRaw(physMaster);

    PtyProxy proxy;
    SerialConfig cfg;
    cfg.device = physSlave;
    QVERIFY(proxy.open(cfg));

    const int appFd = ::open(proxy.slavePath().toLocal8Bit().constData(), O_RDWR | O_NOCTTY);
    QVERIFY(appFd >= 0);
    makeRaw(appFd);

    QString path;
    {
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        path = tmp.fileName();  // keep the name; file is recreated by startCapture
    }
    QVERIFY(proxy.startCapture(path));
    QVERIFY(proxy.isCapturing());

    const QByteArray tx = QByteArray::fromHex("41420D0A");
    QCOMPARE(::write(appFd, tx.constData(), tx.size()), static_cast<ssize_t>(tx.size()));
    QCOMPARE(readWithTimeout(physMaster, tx.size(), 2000), tx);
    QTRY_COMPARE(proxy.stats().tx, static_cast<quint64>(tx.size()));

    proxy.stopCapture();
    QVERIFY(!proxy.isCapturing());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray blob = file.readAll();
    // pcap global header (24) + one record header (16) + RTAC pseudo-header (12).
    QVERIFY(blob.size() >= 24 + 16 + 12 + tx.size());
    // Little-endian magic 0xa1b2c3d4.
    QCOMPARE(static_cast<quint8>(blob.at(0)), static_cast<quint8>(0xd4));
    QCOMPARE(static_cast<quint8>(blob.at(1)), static_cast<quint8>(0xc3));
    // network type == LINKTYPE_RTAC_SERIAL (250) at offset 20.
    QCOMPARE(static_cast<quint8>(blob.at(20)), static_cast<quint8>(250));
    // The payload is present after the headers.
    QVERIFY(blob.contains(tx));

    ::close(appFd);
    proxy.close();
    ::close(physMaster);
}

void BusTest::writeQueueDropsWhenPeerStalls() {
    int physMaster = -1;
    QString physSlave;
    QVERIFY(makeRawPty(physMaster, physSlave));
    makeRaw(physMaster);

    PtyProxy proxy;
    SerialConfig cfg;
    cfg.device = physSlave;
    QVERIFY(proxy.open(cfg));

    // Open the app side but never read it, so the device->app queue backs up.
    const int appFd = ::open(proxy.slavePath().toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    QVERIFY(appFd >= 0);
    makeRaw(appFd);

    QSignalSpy stall(&proxy, &PtyProxy::writeStalled);
    QVERIFY(stall.isValid());

    // Flood the device->app direction until the queue ceiling forces drops.
    const QByteArray blob(static_cast<qsizetype>(64) * 1024, static_cast<char>(0xAB));
    QElapsedTimer timer;
    timer.start();
    while (proxy.stats().dropped == 0 && timer.elapsed() < 8000) {
        const ssize_t n = ::write(physMaster, blob.constData(), blob.size());
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            QTest::qWait(1);  // let the proxy drain physSlave, then retry
        }
        QCoreApplication::processEvents();
    }
    QVERIFY(proxy.stats().dropped > 0);
    QTRY_VERIFY(stall.count() >= 1);

    // The opposite direction still flows — a stalled Rx must not wedge Tx.
    const QByteArray tx = QByteArray::fromHex("99");
    QCOMPARE(::write(appFd, tx.constData(), tx.size()), static_cast<ssize_t>(tx.size()));
    QCOMPARE(readWithTimeout(physMaster, tx.size(), 2000), tx);

    ::close(appFd);
    proxy.close();
    ::close(physMaster);
}

void BusTest::statsCalculatorBasics() {
    StatsCalculator calc;
    calc.setSerialConfig(9600, 8, Parity::None, 1);  // 10 bits per char (1 start, 8 data, 1 stop)

    // Capture Rx chunk at timestamp 1000
    CapturedChunk rx1;
    rx1.dir = Direction::Rx;
    rx1.timestampMs = 1000;
    rx1.data = "hello";  // 5 bytes
    calc.addChunk(rx1);

    QCOMPARE(calc.rxBytes(), static_cast<qint64>(5));
    QCOMPARE(calc.rxChunks(), static_cast<qint64>(1));
    QCOMPARE(calc.rxGap().count, static_cast<qint64>(0));

    // Capture Tx chunk at timestamp 1050
    CapturedChunk tx1;
    tx1.dir = Direction::Tx;
    tx1.timestampMs = 1050;
    tx1.data = "world!!";  // 7 bytes
    calc.addChunk(tx1);

    QCOMPARE(calc.txBytes(), static_cast<qint64>(7));
    QCOMPARE(calc.txChunks(), static_cast<qint64>(1));
    // Latency Tx -> Rx is not measured yet because rx happened before tx,
    // but Rx -> Tx latency (cross gap) should be updated: 1050 - 1000 = 50ms
    QCOMPARE(calc.rxTxGap().count, static_cast<qint64>(1));
    QCOMPARE(calc.rxTxGap().min, static_cast<qint64>(50));
    QCOMPARE(calc.rxTxGap().max, static_cast<qint64>(50));

    // Capture another Rx chunk at timestamp 1200
    CapturedChunk rx2;
    rx2.dir = Direction::Rx;
    rx2.timestampMs = 1200;
    rx2.data = "!";  // 1 byte
    calc.addChunk(rx2);

    // Rx -> Rx gap should be updated: 1200 - 1000 = 200ms
    QCOMPARE(calc.rxGap().count, static_cast<qint64>(1));
    QCOMPARE(calc.rxGap().min, static_cast<qint64>(200));

    // Tx -> Rx latency should be updated: 1200 - 1050 = 150ms
    QCOMPARE(calc.txRxGap().count, static_cast<qint64>(1));
    QCOMPARE(calc.txRxGap().min, static_cast<qint64>(150));

    // Test rates rolling & utilization
    calc.rollRates();
    QCOMPARE(calc.currentRxRate(), static_cast<double>(6));  // 5 + 1
    QCOMPARE(calc.currentTxRate(), static_cast<double>(7));

    // Baud: 9600 bps. bitsPerChar: 10. Max bytes/sec = 960
    // Rx utilization: 6 / 960 * 100 = 0.625 %
    // Tx utilization: 7 / 960 * 100 = 0.729 %
    QCOMPARE(calc.rxBaudUtilization(), 0.625);
}

void BusTest::guiConsoleView() {
    ConsoleView view;
    view.setNewlineMode(ConsoleView::NewlineMode::PerChunk, 0);
    view.setFormats(true, false, false, true); // Hex + ASCII

    CapturedChunk chunk1;
    chunk1.dir = Direction::Rx;
    chunk1.timestampMs = 1000;
    chunk1.data = "ABC";
    view.appendChunk(chunk1);

    // Force synchronous flush to avoid relying on timer loops in tests
    QMetaObject::invokeMethod(&view, "flush");

    // Text should contain plaintext representation: "41 42 43" and "ABC"
    QString plainText = view.toPlainText();
    qDebug() << "DEBUG plaintext:" << plainText;
    QVERIFY(plainText.contains("41 42 43"));
    QVERIFY(plainText.contains("ABC"));

    // Test search query (findQuery)
    view.moveCursorToStart();
    QVERIFY(view.findQuery("42", 0)); // Match middle hex token "42"

    // Test pause behavior
    view.setPaused(true);
    CapturedChunk chunk2;
    chunk2.dir = Direction::Tx;
    chunk2.timestampMs = 2000;
    chunk2.data = "XYZ";
    view.appendChunk(chunk2);
    
    QMetaObject::invokeMethod(&view, "flush");
    // Should NOT contain "XYZ" or "58 59 5A" in rendering while paused
    QVERIFY(!view.toPlainText().contains("XYZ"));
    // But totals should be updated: m_tx count was 0, now it should be 3 bytes
    QCOMPARE(view.txCount(), static_cast<qint64>(3));

    // Resume and flush, "XYZ" should now be rendered
    view.setPaused(false);
    QMetaObject::invokeMethod(&view, "flush");
    QVERIFY(view.toPlainText().contains("XYZ"));
}

void BusTest::guiMacroBar() {
    MacroBar bar;
    QSignalSpy spy(&bar, &MacroBar::send);
    QVERIFY(spy.isValid());

    // Test history recall
    const QByteArray bytes = "macro_payload";
    bar.pushHistory(bytes, true);

    auto *historyBox = bar.findChild<QComboBox*>();
    QVERIFY(historyBox != nullptr);
    QCOMPARE(historyBox->count(), 1);
    
    // Trigger action
    historyBox->setCurrentIndex(0);
    QMetaObject::invokeMethod(&bar, "resendSelected");

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toByteArray(), bytes);
    QCOMPARE(spy.first().at(1).toBool(), true);
}

void BusTest::guiThemeController() {
    ThemeController theme(qApp);
    theme.setMode(ThemeController::Mode::Dark);
    QCOMPARE(theme.mode(), ThemeController::Mode::Dark);

    theme.setMode(ThemeController::Mode::Light);
    QCOMPARE(theme.mode(), ThemeController::Mode::Light);
}

void BusTest::guiMainWindow() {
    MainWindow mainWin;

    // Verify it contains a QTabWidget
    auto *tabWidget = mainWin.findChild<QTabWidget*>();
    QVERIFY(tabWidget != nullptr);

    // Should have 1 tab by default (initial session tab)
    QCOMPARE(tabWidget->count(), 1);

    // Simulate adding session
    QMetaObject::invokeMethod(&mainWin, "addNewSession");
    QCOMPARE(tabWidget->count(), 2);
}

QTEST_MAIN(BusTest)
#include "bus_test.moc"
