#include "core/usb/usb_parser.hpp"
#include <QDir>
#include <QFile>
#include <QHash>
#include <QPair>
#include <QTextStream>
#include <cstring>
#include <mutex>

namespace aether {

namespace {
std::mutex g_cacheMutex;
QHash<QPair<int, int>, QString> g_deviceNameCache;
bool g_cacheInitialized = false;

void initDeviceCache() {
#ifdef Q_OS_LINUX
    QDir dir(QStringLiteral("/sys/bus/usb/devices"));
    if (!dir.exists()) {
        return;
    }

    for (const QString &sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile busFile(dir.filePath(sub + QStringLiteral("/busnum")));
        QFile devFile(dir.filePath(sub + QStringLiteral("/devnum")));
        if (busFile.open(QIODevice::ReadOnly) && devFile.open(QIODevice::ReadOnly)) {
            int bus = busFile.readAll().trimmed().toInt();
            int dev = devFile.readAll().trimmed().toInt();

            QString manuf;
            QFile manufFile(dir.filePath(sub + QStringLiteral("/manufacturer")));
            if (manufFile.open(QIODevice::ReadOnly)) {
                manuf = QString::fromUtf8(manufFile.readAll().trimmed());
            }

            QString prod;
            QFile prodFile(dir.filePath(sub + QStringLiteral("/product")));
            if (prodFile.open(QIODevice::ReadOnly)) {
                prod = QString::fromUtf8(prodFile.readAll().trimmed());
            }

            QString name;
            if (!manuf.isEmpty() && !prod.isEmpty()) {
                name = manuf + QStringLiteral(" ") + prod;
            } else if (!prod.isEmpty()) {
                name = prod;
            } else if (!manuf.isEmpty()) {
                name = manuf;
            }

            if (!name.isEmpty()) {
                g_deviceNameCache.insert(qMakePair(bus, dev), name);
            }
        }
    }
#endif
    g_cacheInitialized = true;
}

QString lookupDeviceName(int bus, int dev) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    auto pair = qMakePair(bus, dev);
    if (g_deviceNameCache.contains(pair)) {
        return g_deviceNameCache.value(pair);
    }
    // Only initialize/rescan if it is a cache miss
    initDeviceCache();
    // If not found in sysfs, cache it as empty to prevent rescanning next time
    if (!g_deviceNameCache.contains(pair)) {
        g_deviceNameCache.insert(pair, QString());
    }
    return g_deviceNameCache.value(pair);
}

QString requestName(quint8 req) {
    switch (req) {
        case 0x00:
            return QStringLiteral("GET_STATUS");
        case 0x01:
            return QStringLiteral("CLEAR_FEATURE");
        case 0x03:
            return QStringLiteral("SET_FEATURE");
        case 0x05:
            return QStringLiteral("SET_ADDRESS");
        case 0x06:
            return QStringLiteral("GET_DESCRIPTOR");
        case 0x07:
            return QStringLiteral("SET_DESCRIPTOR");
        case 0x08:
            return QStringLiteral("GET_CONFIGURATION");
        case 0x09:
            return QStringLiteral("SET_CONFIGURATION");
        case 0x0A:
            return QStringLiteral("GET_INTERFACE");
        case 0x0B:
            return QStringLiteral("SET_INTERFACE");
        case 0x0C:
            return QStringLiteral("SYNCH_FRAME");
        default:
            return QStringLiteral("CUSTOM (0x%1)").arg(req, 2, 16, QLatin1Char('0')).toUpper();
    }
}

QString descriptorTypeName(quint8 type) {
    switch (type) {
        case 1:
            return QStringLiteral("DEVICE");
        case 2:
            return QStringLiteral("CONFIGURATION");
        case 3:
            return QStringLiteral("STRING");
        case 4:
            return QStringLiteral("INTERFACE");
        case 5:
            return QStringLiteral("ENDPOINT");
        case 6:
            return QStringLiteral("DEVICE_QUALIFIER");
        case 7:
            return QStringLiteral("OTHER_SPEED_CONFIGURATION");
        case 8:
            return QStringLiteral("INTERFACE_POWER");
        default:
            return QStringLiteral("UNKNOWN (%1)").arg(type);
    }
}
}  // namespace

UsbUrbInfo UsbParser::parseUrb(const QByteArray &packet) {
    UsbUrbInfo info;
    if (packet.size() < 48) {
        return info;
    }

    const char *data = packet.constData();

    std::memcpy(&info.id, data + 0, 8);
    info.eventType = static_cast<UsbEventType>(data[8]);
    info.transferType = static_cast<UsbTransferType>(data[9]);

    auto endpointNum = static_cast<quint8>(data[10]);
    info.direction = (endpointNum & 0x80) ? Direction::Rx : Direction::Tx;
    info.endpoint = endpointNum & 0x7F;

    info.deviceAddress = static_cast<quint8>(data[11]);
    std::memcpy(&info.busId, data + 12, 2);

    char setupFlag = data[14];
    std::memcpy(&info.status, data + 28, 4);
    std::memcpy(&info.length, data + 32, 4);
    quint32 caplen = 0;
    std::memcpy(&caplen, data + 36, 4);

    int headerSize = 48;
    if (packet.size() >= 64) {
        if (packet.size() >= 64 + static_cast<int>(caplen)) {
            headerSize = 64;
        }
    }

    if (info.transferType == UsbTransferType::Control && setupFlag == 0) {
        info.setupPacket = packet.mid(40, 8);
    }

    if (packet.size() > headerSize) {
        info.payload = packet.mid(headerSize, static_cast<int>(caplen));
    }

    QString eventStr = QStringLiteral("Unknown");
    if (info.eventType == UsbEventType::Submit)
        eventStr = QStringLiteral("SUBMIT");
    else if (info.eventType == UsbEventType::Complete)
        eventStr = QStringLiteral("COMPLETE");
    else if (info.eventType == UsbEventType::Error)
        eventStr = QStringLiteral("ERROR");

    QString typeStr = QStringLiteral("Unknown");
    if (info.transferType == UsbTransferType::Control)
        typeStr = QStringLiteral("CONTROL");
    else if (info.transferType == UsbTransferType::Bulk)
        typeStr = QStringLiteral("BULK");
    else if (info.transferType == UsbTransferType::Interrupt)
        typeStr = QStringLiteral("INTERRUPT");
    else if (info.transferType == UsbTransferType::Isochronous)
        typeStr = QStringLiteral("ISOCHRONOUS");

    QString dirStr = (info.direction == Direction::Rx) ? QStringLiteral("IN") : QStringLiteral("OUT");

    QString devName = lookupDeviceName(info.busId, info.deviceAddress);
    QString devStr = devName.isEmpty() ? QStringLiteral("Dev %1").arg(info.deviceAddress)
                                       : QStringLiteral("%1 (Dev %2)").arg(devName).arg(info.deviceAddress);

    if (info.transferType == UsbTransferType::Control && !info.setupPacket.isEmpty() && info.setupPacket.size() == 8) {
        auto request = static_cast<quint8>(info.setupPacket.at(1));

        quint16 val = 0;
        quint16 idx = 0;
        quint16 len = 0;
        std::memcpy(&val, info.setupPacket.constData() + 2, 2);
        std::memcpy(&idx, info.setupPacket.constData() + 4, 2);
        std::memcpy(&len, info.setupPacket.constData() + 6, 2);

        QString reqName = requestName(request);
        if (request == 0x06) {
            quint8 descType = val >> 8;
            reqName += QStringLiteral(" (%1)").arg(descriptorTypeName(descType));
        }

        info.infoText = QStringLiteral("Control %1: %2 [%3, EP %4] [Value: 0x%5, Index: 0x%6, Len: %7]")
                            .arg(eventStr, reqName)
                            .arg(devStr)
                            .arg(info.endpoint)
                            .arg(QStringLiteral("%1").arg(val, 4, 16, QLatin1Char('0')).toUpper())
                            .arg(QStringLiteral("%1").arg(idx, 4, 16, QLatin1Char('0')).toUpper())
                            .arg(len);
    } else {
        info.infoText = QStringLiteral("%1 %2 %3 [%4, EP %5] Status: %6, Size: %7")
                            .arg(typeStr, dirStr, eventStr)
                            .arg(devStr)
                            .arg(info.endpoint)
                            .arg(info.status)
                            .arg(info.length);
    }

    info.isValid = true;
    return info;
}

QString UsbParser::decodeDescriptor(quint8 descType, const QByteArray &data) {
    QString out;
    QTextStream ts(&out);

    if (descType == 1) {
        if (data.size() < 18) {
            return QStringLiteral("Truncated Device Descriptor (%1/18 bytes)").arg(data.size());
        }
        quint16 bcdUSB = 0;
        std::memcpy(&bcdUSB, data.constData() + 2, 2);

        auto devClass = static_cast<quint8>(data.at(4));
        auto devSubClass = static_cast<quint8>(data.at(5));
        auto devProtocol = static_cast<quint8>(data.at(6));
        auto maxPacketSize = static_cast<quint8>(data.at(7));

        quint16 vid = 0;
        quint16 pid = 0;
        quint16 bcdDevice = 0;
        std::memcpy(&vid, data.constData() + 8, 2);
        std::memcpy(&pid, data.constData() + 10, 2);
        std::memcpy(&bcdDevice, data.constData() + 12, 2);

        ts << "USB Device Descriptor:\n"
           << "  bcdUSB:          " << "0x" << QStringLiteral("%1").arg(bcdUSB, 4, 16, QLatin1Char('0')).toUpper() << "\n"
           << "  bDeviceClass:    " << "0x" << QStringLiteral("%1").arg(devClass, 2, 16, QLatin1Char('0')).toUpper() << "\n"
           << "  bDeviceSubClass: " << "0x" << QStringLiteral("%1").arg(devSubClass, 2, 16, QLatin1Char('0')).toUpper() << "\n"
           << "  bDeviceProtocol: " << "0x" << QStringLiteral("%1").arg(devProtocol, 2, 16, QLatin1Char('0')).toUpper() << "\n"
           << "  bMaxPacketSize0: " << (int)maxPacketSize << "\n"
           << "  idVendor:        " << "0x" << QStringLiteral("%1").arg(vid, 4, 16, QLatin1Char('0')).toUpper() << "\n"
           << "  idProduct:       " << "0x" << QStringLiteral("%1").arg(pid, 4, 16, QLatin1Char('0')).toUpper() << "\n"
           << "  bcdDevice:       " << "0x" << QStringLiteral("%1").arg(bcdDevice, 4, 16, QLatin1Char('0')).toUpper();
    } else if (descType == 2) {
        if (data.size() < 9) {
            return QStringLiteral("Truncated Configuration Descriptor (%1/9 bytes)").arg(data.size());
        }
        quint16 totalLen = 0;
        std::memcpy(&totalLen, data.constData() + 2, 2);

        auto numInterfaces = static_cast<quint8>(data.at(4));
        auto configVal = static_cast<quint8>(data.at(5));
        auto attributes = static_cast<quint8>(data.at(7));
        auto maxPower = static_cast<quint8>(data.at(8));

        ts << "USB Configuration Descriptor:\n"
           << "  wTotalLength:        " << totalLen << "\n"
           << "  bNumInterfaces:      " << (int)numInterfaces << "\n"
           << "  bConfigurationValue: " << (int)configVal << "\n"
           << "  bmAttributes:        " << "0x" << QStringLiteral("%1").arg(attributes, 2, 16, QLatin1Char('0')).toUpper() << "\n"
           << "  bMaxPower:           " << (int)maxPower * 2 << " mA";
    } else {
        ts << "USB Descriptor (Type " << (int)descType << "):\n"
           << "  Raw Data Size: " << data.size() << " bytes\n"
           << "  Hex: " << QString::fromLatin1(data.toHex(' ')).toUpper();
    }

    return out;
}

}  // namespace aether
