#include "bus_test.h"
#include "core/can_backend.h"
#include "core/can_types.h"
#include "core/serial_types.h"
#include "core/stats_calculator.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <cstring>
#include <poll.h>
#include <unistd.h>

using namespace aether;

namespace {

/// First virtual CAN interface present, or empty if none.
QString findVcan() {
    for (const QString &name : CanBackend::listInterfaces()) {
        if (name.startsWith(QLatin1String("vcan"))) {
            return name;
        }
    }
    return {};
}

/// Open an independent raw CAN socket bound to @p iface (FD frames enabled),
/// used by the tests to inject/observe frames alongside the backend.
int openRawCan(const QString &iface) {
    const int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        return -1;
    }
    int on = 1;
    ::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof(on));

    struct ifreq ifr {};
    std::strncpy(ifr.ifr_name, iface.toLocal8Bit().constData(), IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        ::close(fd);
        return -1;
    }
    struct sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

/// Poll @p fd for a single frame up to @p timeoutMs; returns bytes read or -1.
ssize_t recvFrameTimeout(int fd, struct canfd_frame &frame, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        pollfd pfd{fd, POLLIN, 0};
        if (::poll(&pfd, 1, 50) > 0 && (pfd.revents & POLLIN) != 0) {
            const ssize_t n = ::recv(fd, &frame, sizeof(frame), 0);
            if (n > 0) {
                return n;
            }
        }
    }
    return -1;
}

}  // namespace

void BusTest::canConfigValidate() {
    CanConfig cfg;
    QVERIFY(!cfg.validate().isEmpty());  // empty interface rejected
    cfg.iface = QStringLiteral("vcan0");
    QVERIFY(cfg.validate().isEmpty());
    cfg.iface = QString(20, QLatin1Char('a'));  // longer than IFNAMSIZ
    QVERIFY(!cfg.validate().isEmpty());
}

void BusTest::canBackendCapturesFrame() {
    const QString iface = findVcan();
    if (iface.isEmpty()) {
        QSKIP("no vcan interface present (modprobe vcan; ip link add vcan0 type vcan; ip link set up vcan0)");
    }

    CanBackend backend;
    QSignalSpy spy(&backend, &CanBackend::chunkCaptured);

    CanConfig cfg;
    cfg.iface = iface;
    QVERIFY(backend.open(cfg));

    const int tx = openRawCan(iface);
    QVERIFY(tx >= 0);

    struct can_frame frame {};
    frame.can_id = 0x123;
    frame.can_dlc = 4;
    frame.data[0] = 0xDE;
    frame.data[1] = 0xAD;
    frame.data[2] = 0xBE;
    frame.data[3] = 0xEF;
    QCOMPARE(::write(tx, &frame, CAN_MTU), static_cast<ssize_t>(CAN_MTU));

    QVERIFY(spy.wait(1000));

    bool found = false;
    for (const QList<QVariant> &args : spy) {
        const auto chunk = args.at(0).value<aether::CapturedChunk>();
        if (chunk.isFrame && chunk.dir == Direction::Rx && chunk.frameId == 0x123) {
            QCOMPARE(chunk.data, QByteArray::fromHex("DEADBEEF"));
            QVERIFY((chunk.frameFlags & FrameExtendedId) == 0);
            found = true;
        }
    }
    QVERIFY(found);

    ::close(tx);
    backend.close();
}

void BusTest::canBackendTransmitsFrame() {
    const QString iface = findVcan();
    if (iface.isEmpty()) {
        QSKIP("no vcan interface present");
    }

    CanBackend backend;
    CanConfig cfg;
    cfg.iface = iface;
    QVERIFY(backend.open(cfg));

    const int rx = openRawCan(iface);
    QVERIFY(rx >= 0);

    QVERIFY(backend.sendFrame(0x7DF, 0, QByteArray::fromHex("020100")));

    struct canfd_frame frame {};
    const ssize_t n = recvFrameTimeout(rx, frame, 1000);
    QVERIFY(n > 0);
    QCOMPARE(frame.can_id & CAN_SFF_MASK, static_cast<canid_t>(0x7DF));
    QCOMPARE(static_cast<int>(frame.len), 3);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(frame.data), 3), QByteArray::fromHex("020100"));

    ::close(rx);
    backend.close();
}

void BusTest::canBackendFdFrame() {
    const QString iface = findVcan();
    if (iface.isEmpty()) {
        QSKIP("no vcan interface present");
    }

    CanBackend backend;
    CanConfig cfg;
    cfg.iface = iface;
    cfg.fdMode = true;
    QVERIFY(backend.open(cfg));

    const int rx = openRawCan(iface);
    QVERIFY(rx >= 0);

    const QByteArray payload = QByteArray::fromHex("00112233445566778899AABBCCDDEEFF");  // 16 bytes
    QVERIFY(backend.sendFrame(0x321, FrameFd, payload));

    struct canfd_frame frame {};
    const ssize_t n = recvFrameTimeout(rx, frame, 1000);
    if (n != static_cast<ssize_t>(CANFD_MTU)) {
        // vcan may need a larger MTU for FD frames: ip link set vcan0 mtu 72
        QSKIP("CAN-FD frame not received as FD (interface may need 'ip link set <if> mtu 72')");
    }
    QCOMPARE(frame.can_id & CAN_SFF_MASK, static_cast<canid_t>(0x321));
    QCOMPARE(static_cast<int>(frame.len), payload.size());
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(frame.data), payload.size()), payload);

    ::close(rx);
    backend.close();
}

void BusTest::statsCalculatorPerId() {
    StatsCalculator stats;

    const auto frame = [](quint32 id, qint64 ts, int len) {
        CapturedChunk c;
        c.dir = Direction::Rx;
        c.timestampMs = ts;
        c.isFrame = true;
        c.frameId = id;
        c.data = QByteArray(len, '\0');
        return c;
    };

    stats.addChunk(frame(0x100, 1000, 8));
    stats.addChunk(frame(0x100, 1010, 8));
    stats.addChunk(frame(0x200, 1005, 2));

    const auto &perId = stats.perIdStats();
    QCOMPARE(perId.value(0x100).count, quint64{2});
    QCOMPARE(perId.value(0x200).count, quint64{1});
    // The gap between the two 0x100 frames (10 ms) is tracked.
    QCOMPARE(perId.value(0x100).gap.max, qint64{10});

    stats.reset();
    QVERIFY(stats.perIdStats().isEmpty());
}
