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
    loadMacros();
    rebuildMacroButtons();
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
    grid->addWidget(new QLabel(tr("Src Port:")), 3, 0);
    m_srcPortSpin = new QSpinBox(this);
    m_srcPortSpin->setRange(1, 65535);
    m_srcPortSpin->setValue(1234);
    grid->addWidget(m_srcPortSpin, 3, 1);

    grid->addWidget(new QLabel(tr("Dest Port:")), 3, 2);
    m_destPortSpin = new QSpinBox(this);
    m_destPortSpin->setRange(1, 65535);
    m_destPortSpin->setValue(9999);
    grid->addWidget(m_destPortSpin, 3, 3);

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

    m_macroContainer = new QWidget(this);
    m_macroLayout = new QHBoxLayout(m_macroContainer);
    m_macroLayout->setContentsMargins(0, 0, 0, 0);
    m_macroLayout->setSpacing(6);
    macroRow->addWidget(m_macroContainer);

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

void PacketConstructorPanel::warnIfTcpUnsupported() {
    if (m_tcpWarningShown || m_ipProtoBox->currentData().toInt() != 6) {
        return;
    }
    m_tcpWarningShown = true;
    QMessageBox::warning(this, tr("TCP Not Supported"),
                         tr("TCP segment construction (sequence number, flags, checksum) isn't implemented yet — "
                            "the packet will be sent with the IP protocol byte set to TCP but no TCP header, which "
                            "most stacks will reject. Select UDP or ICMP for a valid packet. (Shown once per session.)"));
}

void PacketConstructorPanel::onSendClicked() {
    warnIfTcpUnsupported();
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
        warnIfTcpUnsupported();
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

void PacketConstructorPanel::loadMacros() {
    m_macros.clear();
    QSettings settings;
    const int count = settings.beginReadArray(QStringLiteral("ethernet_macros"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        Macro macro;
        macro.name = settings.value(QStringLiteral("name")).toString();
        macro.rawPacket = settings.value(QStringLiteral("packet")).toByteArray();
        m_macros.append(macro);
    }
    settings.endArray();
}

void PacketConstructorPanel::saveMacros() {
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

void PacketConstructorPanel::rebuildMacroButtons() {
    QLayoutItem *child = nullptr;
    while ((child = m_macroLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    if (m_macros.isEmpty()) {
        m_emptyMacroHint = new QLabel(tr("<i>none yet — click ★ Save as macro</i>"), m_macroContainer);
        m_macroLayout->addWidget(m_emptyMacroHint);
        return;
    }
    m_emptyMacroHint = nullptr;

    for (int i = 0; i < m_macros.size(); ++i) {
        const auto &macro = m_macros.at(i);
        auto *btn = new QPushButton(macro.name, m_macroContainer);
        btn->setObjectName(macro.name);
        btn->setProperty("toolbarAction", true);
        btn->setCursor(Qt::PointingHandCursor);

        const QByteArray preview = macro.rawPacket.left(24).toHex(' ').toUpper();
        btn->setToolTip(tr("%1 bytes: %2%3")
                            .arg(macro.rawPacket.size())
                            .arg(QString::fromLatin1(preview), macro.rawPacket.size() > 24 ? QStringLiteral("…") : QString()));

        connect(btn, &QPushButton::clicked, this, [this, macro] { emit packetReady(macro.rawPacket); });

        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QPushButton::customContextMenuRequested, this, [this, i](const QPoint &pos) {
            auto *senderBtn = qobject_cast<QPushButton *>(sender());
            if (!senderBtn) {
                return;
            }
            QMenu menu(this);
            auto *deleteAct = menu.addAction(tr("Delete Macro"));
            connect(deleteAct, &QAction::triggered, this, [this, i] {
                if (i >= 0 && i < m_macros.size()) {
                    m_macros.removeAt(i);
                    saveMacros();
                    rebuildMacroButtons();
                }
            });
            menu.exec(senderBtn->mapToGlobal(pos));
        });

        m_macroLayout->addWidget(btn);
    }
}

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

    m_macros.append(Macro{name.trimmed(), packet});
    saveMacros();
    rebuildMacroButtons();
}

}  // namespace aether
