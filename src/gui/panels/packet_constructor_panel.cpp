#include "gui/panels/packet_constructor_panel.hpp"
#include "core/common/format_codec.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenu>
#include <QSettings>
#include <QHostAddress>
#include <QtEndian>

namespace aether {

// Helper to calculate standard IP checksums
uint16_t calculateIpChecksum(const uint8_t *data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len - 1; i += 2) {
        sum += (data[i] << 8) | data[i + 1];
    }
    if (len & 1) {
        sum += (data[len - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

QByteArray parseMacAddress(const QString &macStr, bool *ok = nullptr) {
    QByteArray bytes;
    QString cleaned = macStr.simplified();
    cleaned.replace(QStringLiteral(":"), QString());
    cleaned.replace(QStringLiteral("-"), QString());
    bool valid = cleaned.length() >= 12;
    for (int i = 0; i < cleaned.length() && bytes.size() < 6; i += 2) {
        bool byteOk = false;
        bytes.append(static_cast<char>(cleaned.mid(i, 2).toUInt(&byteOk, 16)));
        valid = valid && byteOk;
    }
    while (bytes.size() < 6) {
        bytes.append('\0');
        valid = false;
    }
    if (ok) {
        *ok = valid;
    }
    return bytes;
}

PacketConstructorPanel::PacketConstructorPanel(QWidget *parent) : QWidget(parent) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PacketConstructorPanel::onSendPeriodic);
    buildUi();
    m_macroBar->load();
    m_macroBar->rebuildButtons();
}

void PacketConstructorPanel::buildUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    auto *grid = new QGridLayout();
    grid->setSpacing(6);

    // Layer 2
    grid->addWidget(new QLabel(tr("Src MAC:")), 0, 0);
    m_srcMacEdit = new QLineEdit(QStringLiteral("00:11:22:33:44:55"), this);
    m_srcMacEdit->setObjectName(QStringLiteral("srcMacEdit"));
    grid->addWidget(m_srcMacEdit, 0, 1);

    grid->addWidget(new QLabel(tr("Dest MAC:")), 0, 2);
    m_destMacEdit = new QLineEdit(QStringLiteral("FF:FF:FF:FF:FF:FF"), this);
    m_destMacEdit->setObjectName(QStringLiteral("destMacEdit"));
    grid->addWidget(m_destMacEdit, 0, 3);

    // Layer 3
    grid->addWidget(new QLabel(tr("Src IP:")), 1, 0);
    m_srcIpEdit = new QLineEdit(QStringLiteral("192.168.1.10"), this);
    grid->addWidget(m_srcIpEdit, 1, 1);

    grid->addWidget(new QLabel(tr("Dest IP:")), 1, 2);
    m_destIpEdit = new QLineEdit(QStringLiteral("255.255.255.255"), this);
    grid->addWidget(m_destIpEdit, 1, 3);

    grid->addWidget(new QLabel(tr("TTL:")), 2, 0);
    m_ttlSpin = new QSpinBox(this);
    m_ttlSpin->setRange(1, 255);
    m_ttlSpin->setValue(64);
    grid->addWidget(m_ttlSpin, 2, 1);

    grid->addWidget(new QLabel(tr("Protocol:")), 2, 2);
    m_ipProtoBox = new QComboBox(this);
    m_ipProtoBox->setObjectName(QStringLiteral("protocolCombo"));
    m_ipProtoBox->addItem(QStringLiteral("UDP (17)"), 17);
    m_ipProtoBox->addItem(QStringLiteral("TCP (6)"), 6);
    m_ipProtoBox->addItem(QStringLiteral("ICMP (1)"), 1);
    grid->addWidget(m_ipProtoBox, 2, 3);

    // Layer 4
    m_srcPortLabel = new QLabel(tr("Src Port:"), this);
    grid->addWidget(m_srcPortLabel, 3, 0);
    m_srcPortSpin = new QSpinBox(this);
    m_srcPortSpin->setRange(1, 65535);
    m_srcPortSpin->setValue(1234);
    grid->addWidget(m_srcPortSpin, 3, 1);

    m_destPortLabel = new QLabel(tr("Dest Port:"), this);
    grid->addWidget(m_destPortLabel, 3, 2);
    m_destPortSpin = new QSpinBox(this);
    m_destPortSpin->setRange(1, 65535);
    m_destPortSpin->setValue(9999);
    grid->addWidget(m_destPortSpin, 3, 3);

    // TCP Options
    m_tcpSeqLabel = new QLabel(tr("TCP Seq:"), this);
    grid->addWidget(m_tcpSeqLabel, 4, 0);
    m_tcpSeqEdit = new QLineEdit(QStringLiteral("0"), this);
    m_tcpSeqEdit->setValidator(new QDoubleValidator(0.0, 4294967295.0, 0, this));  // Allow full 32-bit range representation
    grid->addWidget(m_tcpSeqEdit, 4, 1);

    m_tcpAckLabel = new QLabel(tr("TCP Ack:"), this);
    grid->addWidget(m_tcpAckLabel, 4, 2);
    m_tcpAckEdit = new QLineEdit(QStringLiteral("0"), this);
    m_tcpAckEdit->setValidator(new QDoubleValidator(0.0, 4294967295.0, 0, this));
    grid->addWidget(m_tcpAckEdit, 4, 3);

    m_tcpWindowLabel = new QLabel(tr("TCP Window:"), this);
    grid->addWidget(m_tcpWindowLabel, 5, 0);
    m_tcpWindowSpin = new QSpinBox(this);
    m_tcpWindowSpin->setRange(0, 65535);
    m_tcpWindowSpin->setValue(64240);
    grid->addWidget(m_tcpWindowSpin, 5, 1);

    m_tcpFlagsLabel = new QLabel(tr("TCP Flags:"), this);
    grid->addWidget(m_tcpFlagsLabel, 5, 2);

    m_tcpFlagsWidget = new QWidget(this);
    auto *flagsLayout = new QHBoxLayout(m_tcpFlagsWidget);
    flagsLayout->setContentsMargins(0, 0, 0, 0);
    flagsLayout->setSpacing(4);

    m_synCheck = new QCheckBox(tr("SYN"), m_tcpFlagsWidget);
    m_ackCheck = new QCheckBox(tr("ACK"), m_tcpFlagsWidget);
    m_finCheck = new QCheckBox(tr("FIN"), m_tcpFlagsWidget);
    m_rstCheck = new QCheckBox(tr("RST"), m_tcpFlagsWidget);
    m_pshCheck = new QCheckBox(tr("PSH"), m_tcpFlagsWidget);
    m_urgCheck = new QCheckBox(tr("URG"), m_tcpFlagsWidget);

    flagsLayout->addWidget(m_synCheck);
    flagsLayout->addWidget(m_ackCheck);
    flagsLayout->addWidget(m_finCheck);
    flagsLayout->addWidget(m_rstCheck);
    flagsLayout->addWidget(m_pshCheck);
    flagsLayout->addWidget(m_urgCheck);
    grid->addWidget(m_tcpFlagsWidget, 5, 3);

    layout->addLayout(grid);

    // Payload Row
    auto *payloadLayout = new QHBoxLayout();
    payloadLayout->addWidget(new QLabel(tr("Payload:")));
    m_payloadEdit = new QLineEdit(QStringLiteral("DE AD BE EF"), this);
    payloadLayout->addWidget(m_payloadEdit, 1);

    m_payloadFormatBox = new QComboBox(this);
    m_payloadFormatBox->addItem(QStringLiteral("HEX"));
    m_payloadFormatBox->addItem(QStringLiteral("ASCII"));
    payloadLayout->addWidget(m_payloadFormatBox);
    layout->addLayout(payloadLayout);

    const auto markToolbarButton = [](QPushButton *button, const char *kind) {
        button->setProperty(kind, true);
        button->setCursor(Qt::PointingHandCursor);
    };
    const auto makeSectionLabel = [&](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("toolbarSectionLabel"));
        return label;
    };

    auto *macroRow = new QHBoxLayout();
    macroRow->setSpacing(6);
    macroRow->addWidget(makeSectionLabel(tr("Macros")));

    m_macroBar = new EthernetMacroBar(this);
    macroRow->addWidget(m_macroBar);

    macroRow->addStretch(1);

    auto *saveMacroBtn = new QPushButton(tr("★ Save as macro"), this);
    saveMacroBtn->setObjectName(QStringLiteral("saveMacroButton"));
    saveMacroBtn->setToolTip(tr("Save the current packet fields as a quick-send macro"));
    markToolbarButton(saveMacroBtn, "toolbarAction");
    connect(saveMacroBtn, &QPushButton::clicked, this, &PacketConstructorPanel::saveCurrentAsMacro);
    macroRow->addWidget(saveMacroBtn);

    layout->addLayout(macroRow);

    auto *txLayout = new QHBoxLayout();
    m_sendBtn = new QPushButton(tr("Send Packet"), this);
    markToolbarButton(m_sendBtn, "toolbarAction");
    connect(m_sendBtn, &QPushButton::clicked, this, &PacketConstructorPanel::onSendClicked);
    txLayout->addWidget(m_sendBtn);

    m_periodicCheck = new QCheckBox(tr("Periodic"), this);
    connect(m_periodicCheck, &QCheckBox::toggled, this, &PacketConstructorPanel::onPeriodicToggled);
    txLayout->addWidget(m_periodicCheck);

    m_intervalSpin = new QSpinBox(this);
    m_intervalSpin->setRange(1, 10000);
    m_intervalSpin->setValue(1000);
    m_intervalSpin->setSuffix(QStringLiteral(" ms"));
    txLayout->addWidget(m_intervalSpin);

    m_playPcapBtn = new QPushButton(tr("Play PCAP…"), this);
    markToolbarButton(m_playPcapBtn, "toolbarAction");
    connect(m_playPcapBtn, &QPushButton::clicked, this, &PacketConstructorPanel::onLoadPcapClicked);
    txLayout->addWidget(m_playPcapBtn);

    layout->addLayout(txLayout);

    connect(m_ipProtoBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PacketConstructorPanel::updateProtocolFieldsVisibility);
    updateProtocolFieldsVisibility();
}

QByteArray PacketConstructorPanel::buildPacket(bool *ok) {
    QByteArray packet;

    // Ethernet II Header
    bool destMacOk = true;
    bool srcMacOk = true;
    QByteArray dstMac = parseMacAddress(m_destMacEdit->text(), &destMacOk);
    QByteArray srcMac = parseMacAddress(m_srcMacEdit->text(), &srcMacOk);
    if (ok) {
        *ok = destMacOk && srcMacOk;
    }
    packet.append(dstMac);
    packet.append(srcMac);
    packet.append(static_cast<char>(0x08));  // EtherType IPv4 (0x0800)
    packet.append(static_cast<char>(0x00));

    // IP Payload / Transport layer
    QByteArray ipPayload;
    uint8_t proto = static_cast<uint8_t>(m_ipProtoBox->currentData().toInt());

    // Payload text conversion
    QByteArray rawPayload;
    if (m_payloadFormatBox->currentIndex() == 0) {  // HEX
        QString error;
        codec::encodePayload(0, m_payloadEdit->text(), 0, rawPayload, &error);
    } else {
        rawPayload = m_payloadEdit->text().toUtf8();
    }

    if (proto == 17) {  // UDP
        auto srcPort = static_cast<uint16_t>(m_srcPortSpin->value());
        auto destPort = static_cast<uint16_t>(m_destPortSpin->value());
        auto len = static_cast<uint16_t>(8 + rawPayload.size());

        ipPayload.append(static_cast<char>(srcPort >> 8));
        ipPayload.append(static_cast<char>(srcPort & 0xFF));
        ipPayload.append(static_cast<char>(destPort >> 8));
        ipPayload.append(static_cast<char>(destPort & 0xFF));
        ipPayload.append(static_cast<char>(len >> 8));
        ipPayload.append(static_cast<char>(len & 0xFF));
        ipPayload.append(static_cast<char>(0x00));  // Checksum (0 = optional/skipped)
        ipPayload.append(static_cast<char>(0x00));
        ipPayload.append(rawPayload);
    } else if (proto == 1) {  // ICMP Echo Request
        QByteArray icmp;
        icmp.append(static_cast<char>(8));  // Type: Echo Request
        icmp.append(static_cast<char>(0));  // Code
        icmp.append(static_cast<char>(0));  // Checksum placeholder (hi)
        icmp.append(static_cast<char>(0));  // Checksum placeholder (lo)
        icmp.append(static_cast<char>(0));  // Identifier
        icmp.append(static_cast<char>(0));
        icmp.append(static_cast<char>(0));  // Sequence
        icmp.append(static_cast<char>(0));
        icmp.append(rawPayload);

        const uint16_t icmpCsum = calculateIpChecksum(reinterpret_cast<const uint8_t *>(icmp.constData()), icmp.size());
        icmp[2] = static_cast<char>(icmpCsum >> 8);
        icmp[3] = static_cast<char>(icmpCsum & 0xFF);

        ipPayload.append(icmp);
    } else if (proto == 6) {  // TCP
        QByteArray tcp;
        auto srcPort = static_cast<uint16_t>(m_srcPortSpin->value());
        auto destPort = static_cast<uint16_t>(m_destPortSpin->value());

        uint32_t seq = static_cast<uint32_t>(m_tcpSeqEdit->text().toDouble());
        uint32_t ack = static_cast<uint32_t>(m_tcpAckEdit->text().toDouble());

        uint8_t dataOffset = 5 << 4;
        uint8_t flags = 0;
        if (m_synCheck->isChecked())
            flags |= 0x02;
        if (m_ackCheck->isChecked())
            flags |= 0x10;
        if (m_finCheck->isChecked())
            flags |= 0x01;
        if (m_rstCheck->isChecked())
            flags |= 0x04;
        if (m_pshCheck->isChecked())
            flags |= 0x08;
        if (m_urgCheck->isChecked())
            flags |= 0x20;

        auto window = static_cast<uint16_t>(m_tcpWindowSpin->value());

        tcp.append(static_cast<char>(srcPort >> 8));
        tcp.append(static_cast<char>(srcPort & 0xFF));
        tcp.append(static_cast<char>(destPort >> 8));
        tcp.append(static_cast<char>(destPort & 0xFF));

        tcp.append(static_cast<char>(seq >> 24));
        tcp.append(static_cast<char>(seq >> 16));
        tcp.append(static_cast<char>(seq >> 8));
        tcp.append(static_cast<char>(seq & 0xFF));

        tcp.append(static_cast<char>(ack >> 24));
        tcp.append(static_cast<char>(ack >> 16));
        tcp.append(static_cast<char>(ack >> 8));
        tcp.append(static_cast<char>(ack & 0xFF));

        tcp.append(static_cast<char>(dataOffset));
        tcp.append(static_cast<char>(flags));

        tcp.append(static_cast<char>(window >> 8));
        tcp.append(static_cast<char>(window & 0xFF));

        tcp.append(static_cast<char>(0x00));
        tcp.append(static_cast<char>(0x00));

        tcp.append(static_cast<char>(0x00));
        tcp.append(static_cast<char>(0x00));

        tcp.append(rawPayload);

        // Pseudo-Header
        QByteArray pseudoHeader;
        QHostAddress srcAddr(m_srcIpEdit->text());
        QHostAddress destAddr(m_destIpEdit->text());
        uint32_t srcIp = qToBigEndian(srcAddr.toIPv4Address());
        uint32_t destIp = qToBigEndian(destAddr.toIPv4Address());

        pseudoHeader.append(reinterpret_cast<const char *>(&srcIp), 4);
        pseudoHeader.append(reinterpret_cast<const char *>(&destIp), 4);
        pseudoHeader.append(static_cast<char>(0x00));
        pseudoHeader.append(static_cast<char>(0x06));
        auto tcpLen = static_cast<uint16_t>(tcp.size());
        pseudoHeader.append(static_cast<char>(tcpLen >> 8));
        pseudoHeader.append(static_cast<char>(tcpLen & 0xFF));

        QByteArray checksumBuffer = pseudoHeader + tcp;
        uint16_t tcpCsum = calculateIpChecksum(reinterpret_cast<const uint8_t *>(checksumBuffer.constData()), checksumBuffer.size());

        tcp[16] = static_cast<char>(tcpCsum >> 8);
        tcp[17] = static_cast<char>(tcpCsum & 0xFF);

        ipPayload.append(tcp);
    } else {
        ipPayload.append(rawPayload);
    }

    // IP Header (20 bytes)
    QByteArray ipHeader;
    ipHeader.append(static_cast<char>(0x45));  // Version 4, IHL 5
    ipHeader.append(static_cast<char>(0x00));  // TOS
    auto ipLen = static_cast<uint16_t>(20 + ipPayload.size());
    ipHeader.append(static_cast<char>(ipLen >> 8));
    ipHeader.append(static_cast<char>(ipLen & 0xFF));
    ipHeader.append(static_cast<char>(0x00));  // Identification
    ipHeader.append(static_cast<char>(0x00));
    ipHeader.append(static_cast<char>(0x40));  // Flags (Don't Fragment)
    ipHeader.append(static_cast<char>(0x00));
    ipHeader.append(static_cast<char>(m_ttlSpin->value()));
    ipHeader.append(static_cast<char>(proto));
    ipHeader.append(static_cast<char>(0x00));  // Header Checksum placeholder
    ipHeader.append(static_cast<char>(0x00));

    QHostAddress srcAddr(m_srcIpEdit->text());
    QHostAddress destAddr(m_destIpEdit->text());
    uint32_t srcIp = qToBigEndian(srcAddr.toIPv4Address());
    uint32_t destIp = qToBigEndian(destAddr.toIPv4Address());
    ipHeader.append(reinterpret_cast<const char *>(&srcIp), 4);
    ipHeader.append(reinterpret_cast<const char *>(&destIp), 4);

    // Calculate real IPv4 header checksum
    uint16_t csum = calculateIpChecksum(reinterpret_cast<const uint8_t *>(ipHeader.constData()), 20);
    ipHeader[10] = static_cast<char>(csum >> 8);
    ipHeader[11] = static_cast<char>(csum & 0xFF);

    packet.append(ipHeader);
    packet.append(ipPayload);
    return packet;
}

void PacketConstructorPanel::updateProtocolFieldsVisibility() {
    int proto = m_ipProtoBox->currentData().toInt();
    bool isIcmp = (proto == 1);
    bool isTcp = (proto == 6);

    m_srcPortLabel->setVisible(!isIcmp);
    m_srcPortSpin->setVisible(!isIcmp);
    m_destPortLabel->setVisible(!isIcmp);
    m_destPortSpin->setVisible(!isIcmp);

    m_tcpSeqLabel->setVisible(isTcp);
    m_tcpSeqEdit->setVisible(isTcp);
    m_tcpAckLabel->setVisible(isTcp);
    m_tcpAckEdit->setVisible(isTcp);
    m_tcpWindowLabel->setVisible(isTcp);
    m_tcpWindowSpin->setVisible(isTcp);
    m_tcpFlagsLabel->setVisible(isTcp);
    m_tcpFlagsWidget->setVisible(isTcp);
}

void PacketConstructorPanel::onSendClicked() {
    bool ok = true;
    QByteArray pkt = buildPacket(&ok);
    if (!ok) {
        QMessageBox::warning(this, tr("Invalid Packet"), tr("Src/Dest MAC address is not valid hexadecimal — packet not sent."));
        return;
    }
    emit packetReady(pkt);
}

void PacketConstructorPanel::onPeriodicToggled(bool checked) {
    if (checked) {
        m_timer->start(m_intervalSpin->value());
        m_sendBtn->setEnabled(false);
    } else {
        m_timer->stop();
        m_sendBtn->setEnabled(true);
    }
}

void PacketConstructorPanel::onSendPeriodic() {
    bool ok = true;
    QByteArray pkt = buildPacket(&ok);
    if (!ok) {
        m_periodicCheck->setChecked(false);
        QMessageBox::warning(this, tr("Invalid Packet"), tr("Src/Dest MAC address is not valid hexadecimal — periodic send stopped."));
        return;
    }
    emit packetReady(pkt);
}

void PacketConstructorPanel::onLoadPcapClicked() {
    if (m_playPcapBtn->text() == tr("Stop Playback")) {
        emit stopPlaybackRequested();
        m_playPcapBtn->setText(tr("Play PCAP…"));
        return;
    }

    QString path =
        QFileDialog::getOpenFileName(this, tr("Open PCAP file"), QString(), QStringLiteral("PCAP files (*.pcap *.pcapng);;All files (*)"));
    if (!path.isEmpty()) {
        emit playPcapRequested(path);
        m_playPcapBtn->setText(tr("Stop Playback"));
    }
}

void PacketConstructorPanel::resetPlaybackButton() {
    m_playPcapBtn->setText(tr("Play PCAP…"));
}

// ---------------------------------------------------------------------------
// EthernetMacroBar
// ---------------------------------------------------------------------------

PacketConstructorPanel::EthernetMacroBar::EthernetMacroBar(PacketConstructorPanel *parent) : MacroButtonBar(parent), m_panel(parent) {}

void PacketConstructorPanel::EthernetMacroBar::load() {
    m_macros.clear();
    QSettings settings;
    const int count = settings.beginReadArray(QStringLiteral("ethernet_macros"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        EthernetMacro macro;
        macro.name = settings.value(QStringLiteral("name")).toString();
        macro.rawPacket = settings.value(QStringLiteral("packet")).toByteArray();
        m_macros.append(macro);
    }
    settings.endArray();
}

void PacketConstructorPanel::EthernetMacroBar::save() {
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("ethernet_macros"));
    for (int i = 0; i < m_macros.size(); ++i) {
        settings.setArrayIndex(i);
        const auto &macro = m_macros.at(i);
        settings.setValue(QStringLiteral("name"), macro.name);
        settings.setValue(QStringLiteral("packet"), macro.rawPacket);
    }
    settings.endArray();
}

void PacketConstructorPanel::EthernetMacroBar::addMacro(const EthernetMacro &macro) {
    m_macros.append(macro);
    save();
    rebuildButtons();
}

int PacketConstructorPanel::EthernetMacroBar::macroCount() const {
    return m_macros.size();
}

QString PacketConstructorPanel::EthernetMacroBar::macroName(int index) const {
    return m_macros.at(index).name;
}

QString PacketConstructorPanel::EthernetMacroBar::macroToolTip(int index) const {
    const auto &macro = m_macros.at(index);
    const QByteArray preview = macro.rawPacket.left(24).toHex(' ').toUpper();
    return QStringLiteral("%1 bytes: %2%3")
        .arg(macro.rawPacket.size())
        .arg(QString::fromLatin1(preview), macro.rawPacket.size() > 24 ? QStringLiteral("…") : QString());
}

void PacketConstructorPanel::EthernetMacroBar::onMacroTriggered(int index) {
    emit m_panel->packetReady(m_macros.at(index).rawPacket);
}

void PacketConstructorPanel::EthernetMacroBar::buildContextMenu(int index, QMenu &menu) {
    auto *deleteAct = menu.addAction(QStringLiteral("Delete Macro"));
    connect(deleteAct, &QAction::triggered, this, [this, index] {
        if (index >= 0 && index < m_macros.size()) {
            m_macros.removeAt(index);
            save();
            rebuildButtons();
        }
    });
}

// ---------------------------------------------------------------------------
// saveCurrentAsMacro
// ---------------------------------------------------------------------------

void PacketConstructorPanel::saveCurrentAsMacro() {
    bool ok = true;
    QByteArray packet = buildPacket(&ok);
    if (!ok) {
        QMessageBox::warning(this, tr("Invalid Packet"), tr("Src/Dest MAC address is not valid hexadecimal — macro not saved."));
        return;
    }

    bool nameOk = false;
    QString name =
        QInputDialog::getText(this, tr("Save Ethernet Macro"), tr("Enter a name for this macro:"), QLineEdit::Normal, QString(), &nameOk);
    if (!nameOk || name.trimmed().isEmpty()) {
        return;
    }

    m_macroBar->addMacro(EthernetMacro{name.trimmed(), packet});
}

}  // namespace aether
