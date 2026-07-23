#include "core/usb/usb_backend.hpp"
#include "core/usb/usb_parser.hpp"
#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <array>
#include <cstring>
#include <chrono>
#ifdef AETHER_HAVE_LIBUSB
#include <libusb.h>
#endif

namespace aether {

#ifdef AETHER_HAVE_PCAP
namespace {
bool looksLikePermissionError(const QString &pcapError) {
    return pcapError.contains(QStringLiteral("ermission"), Qt::CaseInsensitive);
}

QString rawCaptureHint() {
    return UsbBackend::tr(
        " Raw USB packet capture needs elevated privileges. Either run as root, or grant "
        "this binary capabilities once: 'sudo setcap cap_net_raw,cap_net_admin=eip "
        "<path to aetherbus>' — no root needed after that.");
}
}  // namespace
#endif

UsbBackend::UsbBackend(QObject *parent) : IBusBackend(parent) {
    qRegisterMetaType<aether::CapturedChunk>("aether::CapturedChunk");
    qRegisterMetaType<QVector<aether::CapturedChunk>>("QVector<aether::CapturedChunk>");
    qRegisterMetaType<aether::Direction>("aether::Direction");
}

UsbBackend::~UsbBackend() {
    close();
}

#ifdef AETHER_HAVE_PCAP
bool UsbBackend::open(const UsbConfig &config) {
    close();

    std::array<char, PCAP_ERRBUF_SIZE> errbuf{};
    m_interfaceName = config.interfaceName;

    m_pcapHandle = pcap_open_live(m_interfaceName.toLocal8Bit().constData(),
                                  65535,  // Snapshot length
                                  1,      // Promiscuous mode
                                  10,     // Read timeout in ms
                                  errbuf.data());

    if (!m_pcapHandle) {
        const QString pcapError = QString::fromLocal8Bit(errbuf.data());
        QString message = tr("pcap_open_live failed on %1: %2").arg(m_interfaceName, pcapError);
        if (looksLikePermissionError(pcapError)) {
            message += rawCaptureHint();
        }
        emit errorOccurred(message);
        return false;
    }

    if (pcap_setnonblock(m_pcapHandle, 1, errbuf.data()) < 0) {
        qWarning() << "Failed to set non-blocking mode on pcap handle:" << errbuf.data();
    }

    m_running = true;
    m_stopRequested = false;

    m_captureThread = std::thread(&UsbBackend::runCaptureLoop, this);

    emit started(tr("USB Bus: %1").arg(m_interfaceName));
    return true;
}

void UsbBackend::close() {
    m_stopRequested = true;
    if (m_pcapHandle) {
        pcap_breakloop(m_pcapHandle);
    }

    const bool wasRunning = m_running;

    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }

    {
        std::lock_guard<std::mutex> pcapLock(m_pcapMutex);
        if (m_pcapHandle) {
            pcap_close(m_pcapHandle);
            m_pcapHandle = nullptr;
        }
    }

    m_running = false;
    if (wasRunning) {
        emit stopped();
    }
}

bool UsbBackend::isRunning() const {
    return m_running;
}

std::vector<CapturedChunk> UsbBackend::consumeBufferedChunks() {
    std::lock_guard<std::mutex> bufferLock(m_bufferMutex);
    std::vector<CapturedChunk> temp;
    temp.swap(m_packetBuffer);
    return temp;
}

QStringList UsbBackend::listInterfaces() {
    QStringList names;
    pcap_if_t *alldevs;
    std::array<char, PCAP_ERRBUF_SIZE> errbuf;

    if (pcap_findalldevs(&alldevs, errbuf.data()) == 0) {
        pcap_if_t *d = alldevs;
        while (d) {
            QString name = QString::fromLocal8Bit(d->name);
            if (name.contains(QStringLiteral("usbmon"), Qt::CaseInsensitive) ||
                name.contains(QStringLiteral("USBPcap"), Qt::CaseInsensitive)) {
                names.append(name);
            }
            d = d->next;
        }
        pcap_freealldevs(alldevs);
    }
    return names;
}

void UsbBackend::packetCallback(u_char *user, const struct pcap_pkthdr *header, const u_char *pktData) {
    auto *self = reinterpret_cast<UsbBackend *>(user);

    CapturedChunk chunk;
    chunk.timestampMs = (static_cast<qint64>(header->ts.tv_sec) * 1000) + (header->ts.tv_usec / 1000);
    chunk.data = QByteArray(reinterpret_cast<const char *>(pktData), static_cast<int>(header->caplen));

    // Parse direction from header byte 10
    if (chunk.data.size() >= 48) {
        auto endpointNum = static_cast<quint8>(chunk.data.at(10));
        chunk.dir = (endpointNum & 0x80) ? Direction::Rx : Direction::Tx;
    } else {
        chunk.dir = Direction::Rx;
    }
    chunk.isFrame = false;

    std::lock_guard<std::mutex> bufferLock(self->m_bufferMutex);
    self->m_packetBuffer.push_back(chunk);
}

void UsbBackend::runCaptureLoop() {
    while (!m_stopRequested) {
        int res;
        {
            std::lock_guard<std::mutex> pcapLock(m_pcapMutex);
            res = pcap_dispatch(m_pcapHandle, -1, &UsbBackend::packetCallback, reinterpret_cast<u_char *>(this));
        }
        if (res < 0) {
            if (res != PCAP_ERROR_BREAK && !m_stopRequested) {
                emit errorOccurred(QString::fromLocal8Bit(pcap_geterr(m_pcapHandle)));
                emit disconnected();
            }
            break;
        }

        std::vector<CapturedChunk> batch;
        {
            std::lock_guard<std::mutex> bufferLock(m_bufferMutex);
            batch.swap(m_packetBuffer);
        }

        if (!batch.empty()) {
            QVector<CapturedChunk> qbatch;
            qbatch.reserve(static_cast<int>(batch.size()));
            for (const auto &chunk : batch) {
                qbatch.append(chunk);
            }
            emit chunksCaptured(qbatch);
        }

        // Pacing: only yield thread if there were no packets to process
        if (res == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    m_running = false;
}

uint32_t UsbBackend::linkType() const {
    return m_pcapHandle ? static_cast<uint32_t>(pcap_datalink(m_pcapHandle)) : 220;
}
#else
bool UsbBackend::open(const UsbConfig &) {
    emit errorOccurred(tr("USB capture is not supported on this build (libpcap missing)."));
    return false;
}

void UsbBackend::close() {
    if (m_running) {
        m_running = false;
        emit stopped();
    }
}

bool UsbBackend::isRunning() const {
    return m_running;
}

std::vector<CapturedChunk> UsbBackend::consumeBufferedChunks() {
    return {};
}

QStringList UsbBackend::listInterfaces() {
    return {};
}

void UsbBackend::runCaptureLoop() {}

uint32_t UsbBackend::linkType() const {
    return 220;
}
#endif

#ifdef AETHER_HAVE_LIBUSB
QList<UsbBackend::UsbDeviceInfo> UsbBackend::listDevices() {
    QList<UsbDeviceInfo> result;
    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) < 0) {
        return result;
    }

    libusb_device **devs = nullptr;
    ssize_t cnt = libusb_get_device_list(ctx, &devs);
    if (cnt > 0) {
        for (ssize_t i = 0; i < cnt; ++i) {
            libusb_device *dev = devs[i];
            libusb_device_descriptor desc{};
            if (libusb_get_device_descriptor(dev, &desc) == 0) {
                UsbDeviceInfo info;
                info.vid = desc.idVendor;
                info.pid = desc.idProduct;
                info.busNum = libusb_get_bus_number(dev);
                info.devNum = libusb_get_device_address(dev);

                QString formatted = QStringLiteral("[%1:%2] Bus %3 Device %4")
                                        .arg(info.vid, 4, 16, QLatin1Char('0'))
                                        .toUpper()
                                        .arg(info.pid, 4, 16, QLatin1Char('0'))
                                        .toUpper()
                                        .arg(info.busNum)
                                        .arg(info.devNum);

                info.description = formatted;
                result.append(info);
            }
        }
        libusb_free_device_list(devs, 1);
    }
    libusb_exit(ctx);
    return result;
}

bool UsbBackend::injectControlTransfer(uint16_t vid, uint16_t pid, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue,
                                       uint16_t wIndex, const QByteArray &data, QString *error) {
    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) < 0) {
        if (error) {
            *error = QStringLiteral("Failed to initialize libusb.");
        }
        return false;
    }

    libusb_device_handle *handle = libusb_open_device_with_vid_pid(ctx, vid, pid);
    if (!handle) {
        libusb_exit(ctx);
        if (error) {
            *error = QStringLiteral("Failed to open USB device %1:%2 (Permission denied or unplugged).")
                         .arg(vid, 4, 16, QLatin1Char('0'))
                         .toUpper()
                         .arg(pid, 4, 16, QLatin1Char('0'))
                         .toUpper();
        }
        return false;
    }

    auto *dataPtr = const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(data.constData()));
    auto wLength = static_cast<uint16_t>(data.size());

    int transferred = libusb_control_transfer(handle, bmRequestType, bRequest, wValue, wIndex, dataPtr, wLength, 1000);
    bool ok = true;
    if (transferred < 0) {
        ok = false;
        if (error) {
            *error = QStringLiteral("Control transfer failed: %1").arg(QString::fromLocal8Bit(libusb_error_name(transferred)));
        }
    }

    libusb_close(handle);
    libusb_exit(ctx);
    return ok;
}

bool UsbBackend::injectBulkTransfer(uint16_t vid, uint16_t pid, uint8_t endpoint, const QByteArray &data, QString *error) {
    libusb_context *ctx = nullptr;
    if (libusb_init(&ctx) < 0) {
        if (error) {
            *error = QStringLiteral("Failed to initialize libusb.");
        }
        return false;
    }

    libusb_device_handle *handle = libusb_open_device_with_vid_pid(ctx, vid, pid);
    if (!handle) {
        libusb_exit(ctx);
        if (error) {
            *error = QStringLiteral("Failed to open USB device %1:%2 (Permission denied or unplugged).")
                         .arg(vid, 4, 16, QLatin1Char('0'))
                         .toUpper()
                         .arg(pid, 4, 16, QLatin1Char('0'))
                         .toUpper();
        }
        return false;
    }

    libusb_set_auto_detach_kernel_driver(handle, 1);

    int interfaceNum = 0;
    libusb_claim_interface(handle, interfaceNum);

    auto *dataPtr = const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(data.constData()));
    int transferred = 0;
    int res = libusb_bulk_transfer(handle, endpoint, dataPtr, data.size(), &transferred, 1000);
    bool ok = true;
    if (res < 0) {
        ok = false;
        if (error) {
            *error = QStringLiteral("Bulk transfer failed: %1").arg(QString::fromLocal8Bit(libusb_error_name(res)));
        }
    }

    libusb_release_interface(handle, interfaceNum);
    libusb_close(handle);
    libusb_exit(ctx);
    return ok;
}
#endif

}  // namespace aether
