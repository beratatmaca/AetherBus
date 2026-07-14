#include "gui/sessions/ethernet_session_widget.hpp"
#include "core/ethernet/ethernet_pcap.hpp"
#include "core/common/format_codec.hpp"

#include <QJsonObject>
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
#include <QFontDatabase>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QStyle>
#include <QApplication>

namespace aether {

// Helper to format raw bytes into a Wireshark-like dump, with the same
// HEX/DEC/BIN/ASCII layers the Serial/CAN console toolbar offers (built on
// the same aether::codec primitives it uses) so columns line up row-to-row
// even when the last row is short.
QString formatHexDump(const QByteArray &data, bool showHex, bool showDec, bool showBin, bool showAscii) {
    static constexpr int kHexColWidth = 16 * 2 + 15;
    static constexpr int kDecColWidth = 16 * 3 + 15;
    static constexpr int kBinColWidth = 16 * 8 + 15;

    QString html = QStringLiteral("<pre style='font-family: monospace; line-height: 1.2;'>");
    for (int i = 0; i < data.size(); i += 16) {
        const QByteArray row = data.mid(i, 16);
        html += QStringLiteral("%1  ").arg(i, 4, 16, QChar('0')).toUpper();

        if (showHex) {
            html += codec::toHex(row).leftJustified(kHexColWidth) + QStringLiteral("  ");
        }
        if (showDec) {
            html += codec::toDecimal(row).leftJustified(kDecColWidth) + QStringLiteral("  ");
        }
        if (showBin) {
            html += codec::toBinary(row).leftJustified(kBinColWidth) + QStringLiteral("  ");
        }
        if (showAscii) {
            html += QStringLiteral("|  ") + codec::toAscii(row).toHtmlEscaped();
        }
        html += QStringLiteral("\n");
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

    m_offlineReplayTimer = new QTimer(this);
    m_offlineReplayTimer->setSingleShot(true);
    connect(m_offlineReplayTimer, &QTimer::timeout, this, &EthernetSessionWidget::onOfflineReplayTick);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, [this] { m_stats.rollRates(); });
    m_statsTimer->start();

    buildUi();
    rescanInterfaces();
}

EthernetSessionWidget::~EthernetSessionWidget() {
    m_backend->close();
    m_captureWriter.close();
    m_offlineReplayTimer->stop();
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

    m_mainSplitter = new CollapsibleSplitter(Qt::Horizontal, this);
    m_mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    auto *mainSplitter = m_mainSplitter;

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

    auto *rescanBtn = new QPushButton(configPane);
    rescanBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_BrowserReload));
    rescanBtn->setFixedWidth(32);
    rescanBtn->setToolTip(tr("Rescan network interfaces (F5)"));
    connect(rescanBtn, &QPushButton::clicked, this, &EthernetSessionWidget::rescanInterfaces);
    auto *rescanShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(rescanShortcut, &QShortcut::activated, this, &EthernetSessionWidget::rescanInterfaces);

    auto *interfaceRow = new QHBoxLayout();
    interfaceRow->addWidget(m_interfaceBox, 1);
    interfaceRow->addWidget(rescanBtn);
    configLayout->addLayout(interfaceRow);

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
    // Keeps the toolbar's button row from wrapping/clipping when this session
    // is squeezed into a tile in a multi-session tiled grid.
    rightColumn->setMinimumWidth(420);
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
    saveBtn->setObjectName(QStringLiteral("savePcapButton"));
    markToolbarButton(saveBtn, "toolbarAction");
    connect(saveBtn, &QPushButton::clicked, this, &EthernetSessionWidget::onSavePcap);
    controlsRow->addWidget(saveBtn);

    auto *clearBtn = new QPushButton(tr("Clear"), rightColumn);
    clearBtn->setObjectName(QStringLiteral("clearButton"));
    markToolbarButton(clearBtn, "toolbarAction");
    connect(clearBtn, &QPushButton::clicked, this, &EthernetSessionWidget::onClearLog);
    controlsRow->addWidget(clearBtn);

    m_captureBtn = new QPushButton(tr("Capture"), rightColumn);
    m_captureBtn->setObjectName(QStringLiteral("captureButton"));
    m_captureBtn->setCheckable(true);
    markToolbarButton(m_captureBtn, "toolbarToggle");
    m_captureBtn->setToolTip(tr("Record raw traffic to a pcap file"));
    connect(m_captureBtn, &QPushButton::clicked, this, &EthernetSessionWidget::toggleFileCapture);
    controlsRow->addWidget(m_captureBtn);

    m_replayBtn = new QPushButton(tr("Replay"), rightColumn);
    m_replayBtn->setObjectName(QStringLiteral("replayButton"));
    m_replayBtn->setCheckable(true);
    markToolbarButton(m_replayBtn, "toolbarToggle");
    m_replayBtn->setToolTip(tr("Open and replay a captured pcap file"));
    connect(m_replayBtn, &QPushButton::clicked, this, &EthernetSessionWidget::toggleOfflineReplay);
    controlsRow->addWidget(m_replayBtn);

    m_pauseBtn = new QPushButton(tr("Pause"), rightColumn);
    m_pauseBtn->setObjectName(QStringLiteral("pauseButton"));
    m_pauseBtn->setCheckable(true);
    markToolbarButton(m_pauseBtn, "toolbarToggle");
    m_pauseBtn->setToolTip(tr("Freeze the table; frames keep accumulating and the view catches up on resume"));
    connect(m_pauseBtn, &QPushButton::toggled, this, [this](bool paused) {
        if (paused) {
            return;
        }
        const QVector<CapturedChunk> pending = m_pausedChunks;
        m_pausedChunks.clear();
        appendToPacketList(pending);
    });
    controlsRow->addWidget(m_pauseBtn);

    controlsRow->addStretch(1);
    rightLayout->addLayout(controlsRow);

    // Format-view row for the hex dump pane, mirroring the Serial/CAN console
    // toolbar's HEX/DEC/BIN/ASCII format toggles (same labels/tooltips).
    const auto makeFormatToggle = [&](const QString &objectName, const QString &text, const QString &tooltip, bool checkedByDefault) {
        auto *button = new QPushButton(text, rightColumn);
        button->setObjectName(objectName);
        button->setCheckable(true);
        button->setChecked(checkedByDefault);
        markToolbarButton(button, "toolbarToggle");
        button->setToolTip(tooltip);
        connect(button, &QPushButton::toggled, this, &EthernetSessionWidget::renderSelectedHexDump);
        return button;
    };

    auto *formatRow = new QHBoxLayout();
    formatRow->setSpacing(6);
    formatRow->addWidget(new QLabel(tr("Hex dump:"), rightColumn));
    m_hexCheck = makeFormatToggle(QStringLiteral("hexCheck"), tr("HEX"), tr("Show hexadecimal bytes"), true);
    formatRow->addWidget(m_hexCheck);
    m_decCheck = makeFormatToggle(QStringLiteral("decCheck"), tr("DEC"), tr("Show decimal byte values"), false);
    formatRow->addWidget(m_decCheck);
    m_binCheck = makeFormatToggle(QStringLiteral("binCheck"), tr("BIN"), tr("Show binary byte values"), false);
    formatRow->addWidget(m_binCheck);
    m_asciiCheck = makeFormatToggle(QStringLiteral("asciiCheck"), tr("ASCII"), tr("Show ASCII gutter"), true);
    formatRow->addWidget(m_asciiCheck);
    formatRow->addStretch(1);
    rightLayout->addLayout(formatRow);

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
    m_decodersSplitter = new CollapsibleSplitter(Qt::Vertical, rightSplitter);
    m_decodersSplitter->setObjectName(QStringLiteral("decodersSplitter"));

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
    m_decodersSplitter->setPaneCollapsible(0);

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
    m_mainSplitter->setPaneCollapsible(0);

    outer->addWidget(mainSplitter);
}

void EthernetSessionWidget::startCapture() {
    if (m_backend->isRunning()) {
        stopCapture();
        return;
    }

    if (m_offlineReplayTimer->isActive() || !m_offlineReplayChunks.isEmpty()) {
        emit statusMessage(tr("Stop the offline replay before starting a live capture."), true);
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
    const QString previous = m_interfaceBox->currentText();
    m_interfaceBox->clear();
    m_interfaceBox->addItems(EthernetBackend::listInterfaces());
    if (!previous.isEmpty()) {
        if (m_interfaceBox->findText(previous) < 0) {
            m_interfaceBox->addItem(previous);
        }
        m_interfaceBox->setCurrentText(previous);
    }
}

void EthernetSessionWidget::saveSettings(QSettings &settings) const {
    settings.setValue(QStringLiteral("interface"), m_interfaceBox->currentText());
    settings.setValue(QStringLiteral("bpfFilter"), m_filterEdit->text());
    settings.setValue(QStringLiteral("layout/mainSplitterState"), m_mainSplitter->saveState());
    settings.setValue(QStringLiteral("layout/decodersSplitterState"), m_decodersSplitter->saveState());
}

void EthernetSessionWidget::loadSettings(const QSettings &settings) {
    const QString iface = settings.value(QStringLiteral("interface")).toString();
    if (!iface.isEmpty()) {
        if (m_interfaceBox->findText(iface) < 0) {
            m_interfaceBox->addItem(iface);
        }
        m_interfaceBox->setCurrentText(iface);
    }
    m_filterEdit->setText(settings.value(QStringLiteral("bpfFilter")).toString());
    m_mainSplitter->restoreState(settings.value(QStringLiteral("layout/mainSplitterState")).toByteArray());
    m_decodersSplitter->restoreState(settings.value(QStringLiteral("layout/decodersSplitterState")).toByteArray());
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
    const auto chunks = m_backend->consumeBufferedChunks();
    if (chunks.empty())
        return;

    // Per-packet accounting stays per-chunk; the visible list gets ONE
    // batched model insert + scroll for the whole tick.
    QVector<CapturedChunk> visible;
    QVector<CapturedChunk> all;
    visible.reserve(static_cast<int>(chunks.size()));
    all.reserve(static_cast<int>(chunks.size()));
    const bool paused = m_pauseBtn->isChecked();
    for (const auto &chunk : chunks) {
        m_stats.addChunk(chunk);
        if (m_captureWriter.isOpen()) {
            m_captureWriter.writePacket(chunk.timestampMs, chunk.data);
        }
        all.append(chunk);
        if (paused) {
            m_pausedChunks.append(chunk);
        } else {
            visible.append(chunk);
        }
    }
    appendToPacketList(visible);
    // Control subscribers see all live traffic — the UI Pause only freezes
    // the visible list, not the stream.
    emit controlTraffic(all);
}

void EthernetSessionWidget::processCapturedPacket(const aether::CapturedChunk &chunk) {
    // Counted (and captured-to-disk) immediately even while paused, matching
    // ConsoleView::setPaused's "counted but not rendered until resumed"
    // semantics — Pause only freezes the visible list, not the stats or the
    // continuous file capture below.
    m_stats.addChunk(chunk);

    if (m_captureWriter.isOpen()) {
        m_captureWriter.writePacket(chunk.timestampMs, chunk.data);
    }

    if (m_pauseBtn->isChecked()) {
        m_pausedChunks.append(chunk);
        return;
    }

    appendToPacketList({chunk});
}

void EthernetSessionWidget::appendToPacketList(const QVector<aether::CapturedChunk> &chunks) {
    if (chunks.isEmpty()) {
        return;
    }

    QScrollBar *vbar = m_packetList->verticalScrollBar();
    const bool wasAtBottom = vbar->value() == vbar->maximum();

    m_packetModel->appendPackets(chunks);

    if (wasAtBottom) {
        m_packetList->scrollToBottom();
    }
}

void EthernetSessionWidget::onPacketReady(const QByteArray &packet) {
    if (m_backend->isRunning()) {
        m_backend->sendPacket(packet);
    }
}

bool EthernetSessionWidget::sendControl(const QJsonObject &cmd, QString *error) {
    const auto fail = [&](const QString &msg) {
        if (error != nullptr) {
            *error = msg;
        }
        return false;
    };

    QByteArray frame;
    if (!codec::parseHexString(cmd.value(QStringLiteral("data")).toString(), frame)) {
        return fail(QStringLiteral("ethernet send: 'data' must be a hex string (a full raw frame)"));
    }
    if (frame.isEmpty()) {
        return fail(QStringLiteral("ethernet send: 'data' is empty"));
    }
    if (!m_backend->isRunning()) {
        return fail(QStringLiteral("ethernet send failed — capture not started"));
    }
    m_backend->sendPacket(frame);
    return true;
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

    renderSelectedHexDump();
}

void EthernetSessionWidget::renderSelectedHexDump() {
    const QModelIndex current = m_packetList->selectionModel()->currentIndex();
    if (!current.isValid() || current.row() < 0 || current.row() >= m_packetModel->packetCount())
        return;

    const QByteArray &data = m_packetModel->chunkAt(current.row()).data;
    m_hexDump->setHtml(
        formatHexDump(data, m_hexCheck->isChecked(), m_decCheck->isChecked(), m_binCheck->isChecked(), m_asciiCheck->isChecked()));
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
    auto parsed = readEthernetPcap(filePath, &error);
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

void EthernetSessionWidget::toggleFileCapture() {
    if (m_captureWriter.isOpen()) {
        m_captureWriter.close();
        m_captureBtn->setChecked(false);
        emit statusMessage(tr("Capture stopped."), false);
        return;
    }

    const QString path =
        QFileDialog::getSaveFileName(this, tr("Capture traffic to pcap"), QStringLiteral("aetherbus_ethernet_capture.pcap"),
                                     QStringLiteral("pcap files (*.pcap);;All files (*)"));
    if (path.isEmpty()) {
        m_captureBtn->setChecked(false);
        return;
    }

    QString error;
    if (!m_captureWriter.open(path, &error)) {
        m_captureBtn->setChecked(false);
        emit statusMessage(tr("Could not open capture file: %1").arg(error), true);
        return;
    }

    m_captureBtn->setChecked(true);
    emit statusMessage(tr("Capturing to %1").arg(path), false);
}

void EthernetSessionWidget::toggleOfflineReplay() {
    if (m_offlineReplayTimer->isActive() || !m_offlineReplayChunks.isEmpty()) {
        m_offlineReplayTimer->stop();
        m_offlineReplayChunks.clear();
        m_offlineReplayIndex = 0;
        m_replayBtn->setChecked(false);
        emit statusMessage(tr("Replay stopped."), false);
        return;
    }

    // Offline analysis only; refuse while a live capture owns the packet list.
    if (m_backend->isRunning()) {
        m_replayBtn->setChecked(false);
        emit statusMessage(tr("Stop the live capture before replaying a capture file."), true);
        return;
    }

    const QString path = QFileDialog::getOpenFileName(this, tr("Open capture file for replay"), QString(),
                                                      QStringLiteral("pcap files (*.pcap);;All files (*)"));
    if (path.isEmpty()) {
        m_replayBtn->setChecked(false);
        return;
    }

    QString error;
    auto parsed = readEthernetPcap(path, &error);
    if (!parsed) {
        m_replayBtn->setChecked(false);
        emit statusMessage(tr("Could not open capture: %1").arg(error), true);
        return;
    }

    onClearLog();
    m_offlineReplayChunks = *parsed;
    m_offlineReplayIndex = 0;
    if (m_offlineReplayChunks.isEmpty()) {
        m_replayBtn->setChecked(false);
        emit statusMessage(tr("Capture file has no packets."), false);
        return;
    }

    m_replayBtn->setChecked(true);
    emit statusMessage(tr("Replaying %1 (%2 packets)…").arg(path).arg(m_offlineReplayChunks.size()), false);
    onOfflineReplayTick();
}

void EthernetSessionWidget::onOfflineReplayTick() {
    if (m_offlineReplayIndex >= m_offlineReplayChunks.size()) {
        m_offlineReplayChunks.clear();
        m_offlineReplayIndex = 0;
        m_replayBtn->setChecked(false);
        emit statusMessage(tr("Replay finished."), false);
        return;
    }

    const CapturedChunk chunk = m_offlineReplayChunks[m_offlineReplayIndex];
    processCapturedPacket(chunk);
    ++m_offlineReplayIndex;

    if (m_offlineReplayIndex < m_offlineReplayChunks.size()) {
        qint64 gap = m_offlineReplayChunks[m_offlineReplayIndex].timestampMs - chunk.timestampMs;
        gap = qBound<qint64>(0, gap, static_cast<qint64>(kMaxReplayGapMs));
        m_offlineReplayTimer->start(static_cast<int>(gap));
    } else {
        m_offlineReplayChunks.clear();
        m_offlineReplayIndex = 0;
        m_replayBtn->setChecked(false);
        emit statusMessage(tr("Replay finished."), false);
    }
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

    QString error;
    EthernetPcapWriter writer;
    if (!writer.open(path, &error)) {
        QMessageBox::critical(this, tr("Save PCAP"), tr("Could not open file %1 for writing: %2").arg(path, error));
        return;
    }

    const int packetCount = m_packetModel->packetCount();
    for (int i = 0; i < packetCount; ++i) {
        const CapturedChunk &chunk = m_packetModel->chunkAt(i);
        writer.writePacket(chunk.timestampMs, chunk.data);
    }
    writer.close();

    QMessageBox::information(this, tr("Save PCAP"), tr("Successfully saved %1 packets to %2.").arg(packetCount).arg(path));
}

}  // namespace aether
