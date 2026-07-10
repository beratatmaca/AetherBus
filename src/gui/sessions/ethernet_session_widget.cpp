#include "gui/sessions/ethernet_session_widget.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QHeaderView>
#include <QDateTime>
#include <QHostAddress>
#include <QtEndian>
#include <QDebug>
#include <QMessageBox>
#include <QFileDialog>
#include <QFile>
#include <QDataStream>
#include <QFontDatabase>
#include <QScrollBar>
#include <optional>

namespace aether {

namespace {

constexpr int kPcapGlobalHeaderLen = 24;
constexpr int kPcapRecordHeaderLen = 16;
constexpr quint32 kPcapMagic = 0xa1b2c3d4U;
constexpr quint32 kPcapLinkTypeEthernet = 1;

quint32 readLe32(const QByteArray &blob, int off) {
    return static_cast<quint32>(static_cast<quint8>(blob[off])) | (static_cast<quint32>(static_cast<quint8>(blob[off + 1])) << 8) |
           (static_cast<quint32>(static_cast<quint8>(blob[off + 2])) << 16) |
           (static_cast<quint32>(static_cast<quint8>(blob[off + 3])) << 24);
}

std::optional<QVector<CapturedChunk>> readEthernetPcapFile(const QString &path, QString *error) {
    const auto fail = [&](const QString &reason) -> std::optional<QVector<CapturedChunk>> {
        if (error) {
            *error = reason;
        }
        return std::nullopt;
    };

    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        return fail(EthernetSessionWidget::tr("Cannot open %1: %2").arg(path, file.errorString()));
    }
    const QByteArray blob = file.readAll();
    if (blob.size() < kPcapGlobalHeaderLen) {
        return fail(EthernetSessionWidget::tr("File is too small to be a pcap capture."));
    }
    if (readLe32(blob, 0) != kPcapMagic) {
        return fail(EthernetSessionWidget::tr("Not a little-endian classic pcap file (pcapng is not supported yet)."));
    }
    if (readLe32(blob, 20) != kPcapLinkTypeEthernet) {
        return fail(EthernetSessionWidget::tr("Unsupported link type; expected Ethernet (LINKTYPE_ETHERNET=1)."));
    }

    QVector<CapturedChunk> chunks;
    int pos = kPcapGlobalHeaderLen;
    while (pos + kPcapRecordHeaderLen <= blob.size()) {
        const quint32 sec = readLe32(blob, pos);
        const quint32 usec = readLe32(blob, pos + 4);
        const quint32 inclLen = readLe32(blob, pos + 8);
        pos += kPcapRecordHeaderLen;

        if (pos + static_cast<int>(inclLen) > blob.size()) {
            return fail(EthernetSessionWidget::tr("Truncated record at byte offset %1.").arg(pos));
        }

        CapturedChunk chunk;
        chunk.timestampMs = static_cast<qint64>(sec) * 1000 + static_cast<qint64>(usec) / 1000;
        chunk.data = blob.mid(pos, static_cast<int>(inclLen));
        chunk.dir = Direction::Tx;
        chunk.isFrame = false;
        chunks.append(chunk);

        pos += static_cast<int>(inclLen);
    }
    return chunks;
}

}  // namespace

// Helper to format raw bytes into a nice Wireshark-like hex/ascii dump
QString formatHexDump(const QByteArray &data) {
    QString html = QStringLiteral("<pre style='font-family: monospace; line-height: 1.2;'>");
    for (int i = 0; i < data.size(); i += 16) {
        // Offset
        html += QStringLiteral("%1  ").arg(i, 4, 16, QChar('0')).toUpper();

        // Hex bytes
        QString hexPart;
        QString asciiPart;
        for (int j = 0; j < 16; ++j) {
            if (i + j < data.size()) {
                auto byte = static_cast<uint8_t>(data[i + j]);
                hexPart += QStringLiteral("%1 ").arg(byte, 2, 16, QChar('0')).toUpper();
                asciiPart += (byte >= 32 && byte <= 126) ? static_cast<char>(byte) : '.';
            } else {
                hexPart += QStringLiteral("   ");
            }
        }
        html += hexPart + QStringLiteral("  |  ") + asciiPart.toHtmlEscaped() + QStringLiteral("\n");
    }
    html += QStringLiteral("</pre>");
    return html;
}

EthernetSessionWidget::EthernetSessionWidget(QWidget *parent) : SessionView(parent) {
    m_backend = new EthernetBackend(this);
    connect(m_backend, &EthernetBackend::started, this, &EthernetSessionWidget::onStarted);
    connect(m_backend, &EthernetBackend::stopped, this, &EthernetSessionWidget::onStopped);
    connect(m_backend, &EthernetBackend::errorOccurred, this, &EthernetSessionWidget::onError);
    connect(m_backend, &EthernetBackend::disconnected, this, &EthernetSessionWidget::onDisconnected);

    m_throttleTimer = new QTimer(this);
    connect(m_throttleTimer, &QTimer::timeout, this, &EthernetSessionWidget::onThrottleTimeout);

    m_pcapPlayTimer = new QTimer(this);
    m_pcapPlayTimer->setSingleShot(true);
    connect(m_pcapPlayTimer, &QTimer::timeout, this, &EthernetSessionWidget::onReplayTick);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, [this] { m_stats.rollRates(); });
    m_statsTimer->start();

    buildUi();
    rescanInterfaces();
}

EthernetSessionWidget::~EthernetSessionWidget() {
    m_backend->close();
}

bool EthernetSessionWidget::isRunning() const {
    return m_backend->isRunning();
}

void EthernetSessionWidget::stopSession() {
    m_backend->close();
}

void EthernetSessionWidget::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);

    // Left Column: Config, Stats, and Constructor stacked vertically
    auto *leftColumn = new QWidget(this);
    leftColumn->setMinimumWidth(320);
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    // 1. Interface Selector Pane
    auto *configPane = new QWidget(leftColumn);
    auto *configLayout = new QVBoxLayout(configPane);
    configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->setSpacing(4);

    configLayout->addWidget(new QLabel(tr("Interface:"), configPane));
    m_interfaceBox = new QComboBox(configPane);
    configLayout->addWidget(m_interfaceBox);

    configLayout->addWidget(new QLabel(tr("BPF Filter:"), configPane));
    m_filterEdit = new QLineEdit(configPane);
    m_filterEdit->setPlaceholderText(tr("e.g. port 80 or udp"));
    m_filterEdit->setToolTip(
        tr(R"(Berkeley Packet Filter expression (same syntax as tcpdump), e.g. "port 80", "udp", "host 192.168.1.1".)"));
    configLayout->addWidget(m_filterEdit);

    m_startBtn = new QPushButton(tr("Start Capture"), configPane);
    connect(m_startBtn, &QPushButton::clicked, this, &EthernetSessionWidget::startCapture);
    configLayout->addWidget(m_startBtn);

    leftLayout->addWidget(configPane);

    // 2. Stats Panel
    m_statsPanel = new StatsPanel(leftColumn);
    m_statsPanel->setActiveCalculator(&m_stats);
    m_statsPanel->setMinimumHeight(360);
    leftLayout->addWidget(m_statsPanel, 1);

    mainSplitter->addWidget(leftColumn);

    // Right Column container widget
    auto *rightColumn = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    const auto markToolbarButton = [](QPushButton *button, const char *kind) {
        button->setProperty(kind, true);
        button->setCursor(Qt::PointingHandCursor);
    };

    auto *controlsRow = new QHBoxLayout();
    controlsRow->setSpacing(6);

    auto *saveBtn = new QPushButton(tr("Save PCAP…"), rightColumn);
    markToolbarButton(saveBtn, "toolbarAction");
    connect(saveBtn, &QPushButton::clicked, this, &EthernetSessionWidget::onSavePcap);
    controlsRow->addWidget(saveBtn);

    auto *clearBtn = new QPushButton(tr("Clear Log"), rightColumn);
    markToolbarButton(clearBtn, "toolbarAction");
    connect(clearBtn, &QPushButton::clicked, this, &EthernetSessionWidget::onClearLog);
    controlsRow->addWidget(clearBtn);

    controlsRow->addStretch(1);
    rightLayout->addLayout(controlsRow);

    // Right Column Splitter: Wireshark-Style packet list and decoders
    auto *rightSplitter = new QSplitter(Qt::Vertical, rightColumn);

    // Packet List Table (Top)
    m_packetModel = new EthernetPacketModel(this);
    m_packetList = new QTableView(rightSplitter);
    m_packetList->setModel(m_packetModel);
    m_packetList->setMinimumHeight(80);  // keep at least a few rows visible once the decoders split appears
    m_packetList->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_packetList->horizontalHeader()->setStretchLastSection(true);
    m_packetList->verticalHeader()->setVisible(false);
    m_packetList->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_packetList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_packetList->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &EthernetSessionWidget::onPacketSelected);
    rightSplitter->addWidget(m_packetList);

    // Decoders split (Bottom)
    m_decodersSplitter = new QSplitter(Qt::Vertical, rightSplitter);

    // Packet Details Tree (Bottom Left)
    m_detailTree = new QTreeWidget(m_decodersSplitter);
    m_detailTree->setHeaderLabel(tr("Packet Details"));
    m_decodersSplitter->addWidget(m_detailTree);

    // Hex/ASCII View (Bottom Right)
    m_hexDump = new QTextEdit(m_decodersSplitter);
    m_hexDump->setReadOnly(true);
    QFont hexFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    hexFont.setPointSize(10);
    m_hexDump->setFont(hexFont);
    m_decodersSplitter->addWidget(m_hexDump);

    rightSplitter->addWidget(m_decodersSplitter);
    rightSplitter->setStretchFactor(0, 2);
    rightSplitter->setStretchFactor(1, 1);

    rightLayout->addWidget(rightSplitter, 1);

    // 3. Packet Constructor Panel (at the bottom of the right column)
    m_constructor = new PacketConstructorPanel(rightColumn);
    connect(m_constructor, &PacketConstructorPanel::packetReady, this, &EthernetSessionWidget::onPacketReady);
    connect(m_constructor, &PacketConstructorPanel::playPcapRequested, this, &EthernetSessionWidget::onPlayPcapRequested);
    connect(m_constructor, &PacketConstructorPanel::stopPlaybackRequested, this, &EthernetSessionWidget::onStopPlaybackRequested);
    rightLayout->addWidget(m_constructor);

    mainSplitter->addWidget(rightColumn);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({340, 1100});

    outer->addWidget(mainSplitter);
}

void EthernetSessionWidget::startCapture() {
    if (m_backend->isRunning()) {
        stopCapture();
        return;
    }

    EthernetConfig cfg;
    cfg.interfaceName = m_interfaceBox->currentText();
    cfg.bpfFilter = m_filterEdit->text();
    cfg.promiscuous = true;

    if (m_backend->open(cfg)) {
        m_stats.reset();
        m_packetModel->clearPackets();
        m_detailTree->clear();
        m_hexDump->clear();
    }
}

void EthernetSessionWidget::stopCapture() {
    m_backend->close();
}

void EthernetSessionWidget::rescanInterfaces() {
    m_interfaceBox->clear();
    m_interfaceBox->addItems(EthernetBackend::listInterfaces());
}

void EthernetSessionWidget::onStarted(const QString &info) {
    m_startBtn->setText(tr("Stop Capture"));
    m_interfaceBox->setEnabled(false);
    m_filterEdit->setEnabled(false);
    m_throttleTimer->start(33);  // Throttling update loop at 30 Hz
    emit statusMessage(info, false);
}

void EthernetSessionWidget::onStopped() {
    m_startBtn->setText(tr("Start Capture"));
    m_interfaceBox->setEnabled(true);
    m_filterEdit->setEnabled(true);
    m_throttleTimer->stop();
}

void EthernetSessionWidget::onError(const QString &message) {
    qWarning() << "Ethernet session error:" << message;
    emit statusMessage(message, true);
}

void EthernetSessionWidget::onDisconnected() {
    onStopped();
    emit statusMessage(tr("Interface lost."), true);
    emit sessionTitleChanged(tr("Ethernet Session"));
}

void EthernetSessionWidget::onThrottleTimeout() {
    auto chunks = m_backend->consumeBufferedChunks();
    if (chunks.empty())
        return;
    for (const auto &chunk : chunks) {
        processCapturedPacket(chunk);
    }
}

void EthernetSessionWidget::processCapturedPacket(const aether::CapturedChunk &chunk) {
    m_stats.addChunk(chunk);

    QScrollBar *vbar = m_packetList->verticalScrollBar();
    const bool wasAtBottom = vbar->value() == vbar->maximum();

    m_packetModel->appendPacket(chunk);

    if (wasAtBottom) {
        m_packetList->scrollToBottom();
    }
}

void EthernetSessionWidget::onPacketReady(const QByteArray &packet) {
    if (m_backend->isRunning()) {
        m_backend->sendPacket(packet);
    }
}

void EthernetSessionWidget::onPacketSelected(const QModelIndex &current, const QModelIndex & /*previous*/) {
    if (!current.isValid() || current.row() < 0 || current.row() >= m_packetModel->packetCount())
        return;

    m_detailTree->clear();
    const QByteArray &data = m_packetModel->chunkAt(current.row()).data;

    auto *root = new QTreeWidgetItem(m_detailTree);
    root->setText(0, tr("Frame (%1 bytes)").arg(data.size()));
    parsePacket(data, root);
    m_detailTree->expandAll();

    // Render hex dump
    m_hexDump->setHtml(formatHexDump(data));
}

void EthernetSessionWidget::parsePacket(const QByteArray &data, QTreeWidgetItem *root) {
    if (data.size() < 14) {
        new QTreeWidgetItem(root, QStringList() << tr("Malformed Ethernet packet (less than 14 bytes)"));
        return;
    }

    // Ethernet II
    const auto *mac = reinterpret_cast<const uint8_t *>(data.constData());
    QString dest = QStringLiteral("%1:%2:%3:%4:%5:%6")
                       .arg(mac[0], 2, 16, QChar('0'))
                       .arg(mac[1], 2, 16, QChar('0'))
                       .arg(mac[2], 2, 16, QChar('0'))
                       .arg(mac[3], 2, 16, QChar('0'))
                       .arg(mac[4], 2, 16, QChar('0'))
                       .arg(mac[5], 2, 16, QChar('0'))
                       .toUpper();
    QString src = QStringLiteral("%1:%2:%3:%4:%5:%6")
                      .arg(mac[6], 2, 16, QChar('0'))
                      .arg(mac[7], 2, 16, QChar('0'))
                      .arg(mac[8], 2, 16, QChar('0'))
                      .arg(mac[9], 2, 16, QChar('0'))
                      .arg(mac[10], 2, 16, QChar('0'))
                      .arg(mac[11], 2, 16, QChar('0'))
                      .toUpper();
    uint16_t etherType = (mac[12] << 8) | mac[13];

    auto *ethNode = new QTreeWidgetItem(root, QStringList() << tr("Ethernet II (Src: %1, Dst: %2)").arg(src, dest));
    new QTreeWidgetItem(ethNode, QStringList() << tr("Destination MAC: %1").arg(dest));
    new QTreeWidgetItem(ethNode, QStringList() << tr("Source MAC: %1").arg(src));
    new QTreeWidgetItem(ethNode, QStringList() << tr("Type: 0x%1").arg(etherType, 4, 16, QChar('0')).toUpper());

    if (etherType == 0x0800) {  // IPv4
        if (data.size() < 34) {
            new QTreeWidgetItem(root, QStringList() << tr("IPv4 packet truncated"));
            return;
        }

        const uint8_t *ip = mac + 14;
        uint8_t version = ip[0] >> 4;
        uint8_t ihl = ip[0] & 0x0F;
        uint16_t totalLen = (ip[2] << 8) | ip[3];
        uint8_t ttl = ip[8];
        uint8_t proto = ip[9];
        QString srcIp = QHostAddress(qFromBigEndian<quint32>(ip + 12)).toString();
        QString destIp = QHostAddress(qFromBigEndian<quint32>(ip + 16)).toString();

        auto *ipNode = new QTreeWidgetItem(root, QStringList() << tr("Internet Protocol Version 4 (Src: %1, Dst: %2)").arg(srcIp, destIp));
        new QTreeWidgetItem(ipNode, QStringList() << tr("Version: %1").arg(version));
        new QTreeWidgetItem(ipNode, QStringList() << tr("Header Length: %1 bytes (%2 words)").arg(ihl * 4).arg(ihl));
        new QTreeWidgetItem(ipNode, QStringList() << tr("Total Length: %1").arg(totalLen));
        new QTreeWidgetItem(ipNode, QStringList() << tr("TTL: %1").arg(ttl));
        new QTreeWidgetItem(ipNode, QStringList() << tr("Protocol: %1").arg(proto));

        int ipHeaderLen = ihl * 4;
        if (proto == 17 && data.size() >= 14 + ipHeaderLen + 8) {  // UDP
            const uint8_t *udp = ip + ipHeaderLen;
            uint16_t srcPort = (udp[0] << 8) | udp[1];
            uint16_t destPort = (udp[2] << 8) | udp[3];
            uint16_t udpLen = (udp[4] << 8) | udp[5];

            auto *udpNode = new QTreeWidgetItem(
                root, QStringList() << tr("User Datagram Protocol (Src Port: %1, Dst Port: %2)").arg(srcPort).arg(destPort));
            new QTreeWidgetItem(udpNode, QStringList() << tr("Source Port: %1").arg(srcPort));
            new QTreeWidgetItem(udpNode, QStringList() << tr("Destination Port: %1").arg(destPort));
            new QTreeWidgetItem(udpNode, QStringList() << tr("Length: %1").arg(udpLen));
        }
    }
}

void EthernetSessionWidget::onPlayPcapRequested(const QString &filePath) {
    if (!m_backend->isRunning()) {
        QMessageBox::warning(this, tr("Play PCAP"),
                             tr("Start a capture before replaying a PCAP file — packets are injected onto the active interface."));
        m_constructor->resetPlaybackButton();
        return;
    }

    QString error;
    auto parsed = readEthernetPcapFile(filePath, &error);
    if (!parsed) {
        QMessageBox::warning(this, tr("Play PCAP"), tr("Could not load %1: %2").arg(filePath, error));
        m_constructor->resetPlaybackButton();
        return;
    }

    m_replayChunks = *parsed;
    m_replayIndex = 0;
    if (m_replayChunks.isEmpty()) {
        m_constructor->resetPlaybackButton();
        return;
    }
    onReplayTick();
}

void EthernetSessionWidget::onStopPlaybackRequested() {
    m_pcapPlayTimer->stop();
    m_replayChunks.clear();
    m_replayIndex = 0;
}

void EthernetSessionWidget::onReplayTick() {
    if (m_replayIndex >= m_replayChunks.size() || !m_backend->isRunning()) {
        m_replayChunks.clear();
        m_replayIndex = 0;
        m_constructor->resetPlaybackButton();
        return;
    }

    const CapturedChunk chunk = m_replayChunks[m_replayIndex];
    m_backend->sendPacket(chunk.data);
    ++m_replayIndex;

    if (m_replayIndex < m_replayChunks.size()) {
        qint64 gap = m_replayChunks[m_replayIndex].timestampMs - chunk.timestampMs;
        gap = qBound<qint64>(0, gap, static_cast<qint64>(kMaxReplayGapMs));
        m_pcapPlayTimer->start(static_cast<int>(gap));
    } else {
        m_replayChunks.clear();
        m_replayIndex = 0;
        m_constructor->resetPlaybackButton();
    }
}

void EthernetSessionWidget::onClearLog() {
    m_packetModel->clearPackets();
    m_detailTree->clear();
    m_hexDump->clear();
    m_stats.reset();
}

void EthernetSessionWidget::onSavePcap() {
    if (m_packetModel->packetCount() == 0) {
        QMessageBox::information(this, tr("Save PCAP"), tr("No packets captured to save."));
        return;
    }

    QString path = QFileDialog::getSaveFileName(this, tr("Save PCAP file"), QStringLiteral("aetherbus_ethernet.pcap"),
                                                QStringLiteral("PCAP files (*.pcap);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QFile::WriteOnly)) {
        QMessageBox::critical(this, tr("Save PCAP"), tr("Could not open file %1 for writing.").arg(path));
        return;
    }

    // Write standard PCAP global header (24 bytes)
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    out << static_cast<quint32>(0xa1b2c3d4);  // Magic number
    out << static_cast<quint16>(2);           // Version major
    out << static_cast<quint16>(4);           // Version minor
    out << static_cast<qint32>(0);            // GMT to local correction
    out << static_cast<quint32>(0);           // Accuracy of timestamps
    out << static_cast<quint32>(65535);       // Max length of captured packets
    out << static_cast<quint32>(1);           // Data link type (LINKTYPE_ETHERNET = 1)

    // Write packets
    const int packetCount = m_packetModel->packetCount();
    for (int i = 0; i < packetCount; ++i) {
        const CapturedChunk &chunk = m_packetModel->chunkAt(i);

        auto sec = static_cast<quint32>(chunk.timestampMs / 1000);
        auto usec = static_cast<quint32>((chunk.timestampMs % 1000) * 1000);
        auto len = static_cast<quint32>(chunk.data.size());

        out << sec;
        out << usec;
        out << len;  // Saved size
        out << len;  // Original size

        file.write(chunk.data);
    }

    file.close();
    QMessageBox::information(this, tr("Save PCAP"), tr("Successfully saved %1 packets to %2.").arg(packetCount).arg(path));
}

}  // namespace aether
