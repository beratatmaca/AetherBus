#include "gui/panels/usb_config_panel.hpp"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QStyle>
#include <QApplication>

namespace aether {

UsbConfigPanel::UsbConfigPanel(QWidget *parent) : QGroupBox(QStringLiteral("USB Interface"), parent) {
    auto *form = new QFormLayout(this);
    form->setContentsMargins(6, 6, 6, 6);
    form->setSpacing(4);

    m_interfaceBox = new QComboBox(this);
    m_interfaceBox->setEditable(true);
    m_interfaceBox->setToolTip(QStringLiteral("Select or type the USB bus/interface name (e.g., usbmon0, USBPcap1)"));

    auto *rescanBtn = new QPushButton(this);
    rescanBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_BrowserReload));
    rescanBtn->setFixedWidth(32);
    rescanBtn->setToolTip(QStringLiteral("Rescan USB capture interfaces (F5)"));
    connect(rescanBtn, &QPushButton::clicked, this, &UsbConfigPanel::rescanRequested);

    auto *interfaceRow = new QHBoxLayout();
    interfaceRow->addWidget(m_interfaceBox, 1);
    interfaceRow->addWidget(rescanBtn);
    form->addRow(QStringLiteral("Capture interface"), interfaceRow);

    m_startButton = new QPushButton(QStringLiteral("Start Capture"), this);
    m_startButton->setToolTip(QStringLiteral("Open interface and monitor USB transfers"));
    connect(m_startButton, &QPushButton::clicked, this, &UsbConfigPanel::onStartButtonClicked);
    form->addRow(m_startButton);

    m_statusLabel = new QLabel(QStringLiteral("Idle."), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextFormat(Qt::RichText);
    form->addRow(m_statusLabel);

    auto *helperLabel = new QLabel(this);
    helperLabel->setTextFormat(Qt::RichText);
    helperLabel->setWordWrap(true);
    helperLabel->setText(
        QStringLiteral("<span style='color:#888888; font-size:11px;'>"
                       "<b>Tip:</b> usbmon interfaces sniff entire USB buses (e.g., <i>usbmon3</i> captures Bus 3). "
                       "Run <font face='monospace'>lsusb</font> to find your target device's Bus number.<br/><br/>"
                       "<b>Linux Permissions:</b> If no interfaces appear, load the module and grant access:<br/>"
                       "<font face='monospace'>sudo modprobe usbmon<br/>"
                       "sudo chmod +r /dev/usbmon*</font>"
                       "</span>"));
    form->addRow(helperLabel);
}

UsbConfig UsbConfigPanel::config() const {
    UsbConfig cfg;
    cfg.interfaceName = m_interfaceBox->currentText().trimmed();
    return cfg;
}

void UsbConfigPanel::setRunning(bool running) {
    m_running = running;
    m_interfaceBox->setEnabled(!running);
    if (running) {
        m_startButton->setText(QStringLiteral("Stop Capture"));
    } else {
        m_startButton->setText(QStringLiteral("Start Capture"));
        setStatus(QStringLiteral("Idle."));
    }
}

void UsbConfigPanel::setInterfaces(const QStringList &list) {
    const QString current = m_interfaceBox->currentText();
    m_interfaceBox->clear();
    m_interfaceBox->addItems(list);
    if (!current.isEmpty()) {
        m_interfaceBox->setCurrentText(current);
    }
}

void UsbConfigPanel::setInterfaceName(const QString &name) {
    m_interfaceBox->setCurrentText(name);
}

void UsbConfigPanel::setStatus(const QString &htmlText) {
    m_statusLabel->setText(htmlText);
}

void UsbConfigPanel::onStartButtonClicked() {
    if (m_running) {
        emit stopUsb();
    } else {
        emit startUsb(config());
    }
}

}  // namespace aether
