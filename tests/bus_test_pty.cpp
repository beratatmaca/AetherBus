#include "bus_test.h"
#include "core/capture_replay.h"
#include "core/pty_proxy.h"
#include "core/serial_types.h"
#include "core/signal_cleanup.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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
    cfg.flow = FlowControl::RtsCts;
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

    const QByteArray tx = QByteArray::fromHex("4142");
    QCOMPARE(::write(appFd, tx.constData(), tx.size()), static_cast<ssize_t>(tx.size()));
    QCOMPARE(readWithTimeout(physMaster, tx.size(), 2000), tx);
    const QByteArray rx = QByteArray::fromHex("5A5B");
    QCOMPARE(::write(physMaster, rx.constData(), rx.size()), static_cast<ssize_t>(rx.size()));
    QCOMPARE(readWithTimeout(appFd, rx.size(), 2000), rx);

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

    termios tio{};
    QCOMPARE(::tcgetattr(appFd, &tio), 0);
    ::cfmakeraw(&tio);
    ::cfsetispeed(&tio, B9600);
    ::cfsetospeed(&tio, B9600);
    QCOMPARE(::tcsetattr(appFd, TCSANOW, &tio), 0);

    QVERIFY(spy.wait(2000));
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 9600);

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
        path = tmp.fileName();
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
    QVERIFY(blob.size() >= 24 + 16 + 12 + tx.size());
    QCOMPARE(static_cast<quint8>(blob.at(0)), static_cast<quint8>(0xd4));
    QCOMPARE(static_cast<quint8>(blob.at(1)), static_cast<quint8>(0xc3));
    QCOMPARE(static_cast<quint8>(blob.at(20)), static_cast<quint8>(250));
    QVERIFY(blob.contains(tx));

    ::close(appFd);
    proxy.close();
    ::close(physMaster);
}

void BusTest::captureReplayRoundTrip() {
    int physMaster = -1;
    QString physSlave;
    QVERIFY(makeRawPty(physMaster, physSlave));

    PtyProxy proxy;
    SerialConfig cfg;
    cfg.device = physSlave;
    QVERIFY(proxy.open(cfg));

    QString path;
    {
        QTemporaryFile tmp;
        QVERIFY(tmp.open());
        path = tmp.fileName();
    }
    QVERIFY(proxy.startCapture(path));

    const QByteArray txBytes = QByteArray::fromHex("41420D0A");
    const QByteArray rxBytes = QByteArray::fromHex("0663");
    QVERIFY(proxy.injectToDevice(txBytes));
    QVERIFY(proxy.injectToApp(rxBytes));

    proxy.stopCapture();
    proxy.close();
    ::close(physMaster);

    QString error;
    const auto chunks = readRtacPcap(path, &error);
    QVERIFY2(chunks.has_value(), qPrintable(error));
    if (!chunks.has_value()) {
        return;  // explicit guard so static analysis sees the optional is checked
    }
    const auto &chunkList = *chunks;
    QCOMPARE(chunkList.size(), 2);
    QCOMPARE(chunkList.at(0).dir, Direction::Tx);
    QCOMPARE(chunkList.at(0).data, txBytes);
    QCOMPARE(chunkList.at(1).dir, Direction::Rx);
    QCOMPARE(chunkList.at(1).data, rxBytes);
    QVERIFY(chunkList.at(0).timestampMs > 0);
    QVERIFY(chunkList.at(1).timestampMs >= chunkList.at(0).timestampMs);

    QString err2;
    QVERIFY(!readRtacPcap(QStringLiteral("/nonexistent/aetherbus_missing.pcap"), &err2).has_value());
    QVERIFY(!err2.isEmpty());
}

void BusTest::crashCleanupUnlinksSymlink() {
    // Stand in for a slave-PTY symlink: a link pointing at a real temp file.
    QTemporaryFile target;
    QVERIFY(target.open());
    const QString targetPath = target.fileName();
    const QString linkPath = targetPath + QStringLiteral(".sniff_link");
    const QByteArray linkC = linkPath.toLocal8Bit();
    ::unlink(linkC.constData());  // clear any leftover from a prior run
    QCOMPARE(::symlink(targetPath.toLocal8Bit().constData(), linkC.constData()), 0);
    QVERIFY(QFileInfo(linkPath).isSymLink());

    // Registering then running the emergency path (what the signal handler calls)
    // must remove the symlink — the resource a sudden exit would otherwise leak.
    const int slot = registerCleanup(linkC.constData(), -1, -1);
    QVERIFY(slot >= 0);
    runEmergencyCleanup();
    QVERIFY(!QFileInfo(linkPath).isSymLink());
    releaseCleanup(slot);

    // A released slot must NOT be cleaned: re-create, release, verify it survives.
    QCOMPARE(::symlink(targetPath.toLocal8Bit().constData(), linkC.constData()), 0);
    const int slot2 = registerCleanup(linkC.constData(), -1, -1);
    QVERIFY(slot2 >= 0);
    releaseCleanup(slot2);
    runEmergencyCleanup();
    QVERIFY(QFileInfo(linkPath).isSymLink());

    ::unlink(linkC.constData());
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

    const int appFd = ::open(proxy.slavePath().toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    QVERIFY(appFd >= 0);
    makeRaw(appFd);

    QSignalSpy stall(&proxy, &PtyProxy::writeStalled);
    QVERIFY(stall.isValid());

    const QByteArray blob(static_cast<qsizetype>(64) * 1024, static_cast<char>(0xAB));
    QElapsedTimer timer;
    timer.start();
    while (proxy.stats().dropped == 0 && timer.elapsed() < 8000) {
        const ssize_t n = ::write(physMaster, blob.constData(), blob.size());
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            QTest::qWait(1);
        }
        QCoreApplication::processEvents();
    }
    QVERIFY(proxy.stats().dropped > 0);
    QTRY_VERIFY(stall.count() >= 1);

    const QByteArray tx = QByteArray::fromHex("99");
    QCOMPARE(::write(appFd, tx.constData(), tx.size()), static_cast<ssize_t>(tx.size()));
    QCOMPARE(readWithTimeout(physMaster, tx.size(), 2000), tx);

    ::close(appFd);
    proxy.close();
    ::close(physMaster);
}
