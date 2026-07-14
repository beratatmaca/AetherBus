#include "gui/panels/usb_injection_panel.hpp"
#include "core/usb/usb_backend.hpp"
#include "core/common/format_codec.hpp"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QLabel>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>

namespace aether {

UsbInjectionPanel::UsbInjectionPanel(QWidget *parent) : QGroupBox(QStringLiteral("USB Injection"), parent) {
    buildUi();
    rescanDevices();
}

void UsbInjectionPanel::rescanDevices() {
    m_deviceBox->clear();
#ifdef AETHER_HAVE_LIBUSB
    const auto list = UsbBackend::listDevices();
    for (const auto &device : list) {
        m_deviceBox->addItem(device.description, QVariant::fromValue(QPair<uint16_t, uint16_t>(device.vid, device.pid)));
    }
#else
    m_deviceBox->addItem(QStringLiteral("libusb not compiled"));
#endif
}

void UsbInjectionPanel::onTransferTypeChanged(int index) {
    m_stackedWidget->setCurrentIndex(index);
}

void UsbInjectionPanel::onInjectButtonClicked() {
#ifndef AETHER_HAVE_LIBUSB
    QMessageBox::warning(this, tr("Not Supported"), tr("libusb injection is not compiled in this build."));
    return;
#else
    if (m_deviceBox->currentIndex() < 0) {
        QMessageBox::warning(this, tr("Warning"), tr("Please select a target USB device."));
        return;
    }

    const auto dataPair = m_deviceBox->currentData().value<QPair<uint16_t, uint16_t> >();
    uint16_t vid = dataPair.first;
    uint16_t pid = dataPair.second;

    QString error;
    bool success = false;

    if (m_typeBox->currentIndex() == 0) {  // Control
        bool ok = false;
        uint8_t reqType = static_cast<uint8_t>(m_reqTypeEdit->text().trimmed().toUShort(&ok, 16));
        if (!ok) {
            QMessageBox::warning(this, tr("Format Error"), tr("Request Type must be a hex byte (e.g. 40)."));
            return;
        }

        uint8_t req = static_cast<uint8_t>(m_reqEdit->text().trimmed().toUShort(&ok, 16));
        if (!ok) {
            QMessageBox::warning(this, tr("Format Error"), tr("Request must be a hex byte (e.g. 01)."));
            return;
        }

        uint16_t val = m_valEdit->text().trimmed().toUShort(&ok, 16);
        if (!ok) {
            QMessageBox::warning(this, tr("Format Error"), tr("Value must be a hex word (e.g. 0100)."));
            return;
        }

        uint16_t idx = m_idxEdit->text().trimmed().toUShort(&ok, 16);
        if (!ok) {
            QMessageBox::warning(this, tr("Format Error"), tr("Index must be a hex word (e.g. 0000)."));
            return;
        }

        QByteArray payload;
        const QString payloadStr = m_controlDataEdit->text().trimmed();
        if (!payloadStr.isEmpty() && !codec::parseCompactHex(payloadStr, payload)) {
            QMessageBox::warning(this, tr("Format Error"), tr("Payload data must be a hex string."));
            return;
        }

        success = UsbBackend::injectControlTransfer(vid, pid, reqType, req, val, idx, payload, &error);
    } else {  // Bulk
        bool ok = false;
        uint8_t endpoint = static_cast<uint8_t>(m_epEdit->text().trimmed().toUShort(&ok, 16));
        if (!ok) {
            QMessageBox::warning(this, tr("Format Error"), tr("Endpoint must be a hex byte (e.g. 02 or 82)."));
            return;
        }

        QByteArray payload;
        const QString payloadStr = m_bulkDataEdit->text().trimmed();
        if (!payloadStr.isEmpty() && !codec::parseCompactHex(payloadStr, payload)) {
            QMessageBox::warning(this, tr("Format Error"), tr("Payload data must be a hex string."));
            return;
        }

        success = UsbBackend::injectBulkTransfer(vid, pid, endpoint, payload, &error);
    }

    if (success) {
        QMessageBox::information(this, tr("Success"), tr("USB packet injected successfully!"));
    } else {
        QMessageBox::critical(this, tr("Injection Failed"), error);
    }
#endif
}

void UsbInjectionPanel::buildUi() {
    auto *form = new QFormLayout(this);
    form->setContentsMargins(6, 6, 6, 6);
    form->setSpacing(4);

    m_deviceBox = new QComboBox(this);
    m_deviceBox->setToolTip(QStringLiteral("Target device to inject transfers to"));

    auto *rescanBtn = new QPushButton(this);
    rescanBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_BrowserReload));
    rescanBtn->setFixedWidth(32);
    rescanBtn->setToolTip(QStringLiteral("Rescan connected USB devices"));
    connect(rescanBtn, &QPushButton::clicked, this, &UsbInjectionPanel::rescanDevices);

    auto *devRow = new QHBoxLayout();
    devRow->addWidget(m_deviceBox, 1);
    devRow->addWidget(rescanBtn);
    form->addRow(QStringLiteral("Target device"), devRow);

    m_typeBox = new QComboBox(this);
    m_typeBox->addItem(QStringLiteral("Control Transfer"), 0);
    m_typeBox->addItem(QStringLiteral("Bulk Transfer"), 1);
    connect(m_typeBox, &QComboBox::currentIndexChanged, this, &UsbInjectionPanel::onTransferTypeChanged);
    form->addRow(QStringLiteral("Transfer type"), m_typeBox);

    m_stackedWidget = new QStackedWidget(this);

    // Control container
    m_controlContainer = new QWidget(m_stackedWidget);
    auto *controlForm = new QFormLayout(m_controlContainer);
    controlForm->setContentsMargins(0, 0, 0, 0);
    controlForm->setSpacing(4);

    m_reqTypeEdit = new QLineEdit(m_controlContainer);
    m_reqTypeEdit->setPlaceholderText(QStringLiteral("e.g. 40 (hex)"));
    controlForm->addRow(QStringLiteral("bmRequestType"), m_reqTypeEdit);

    m_reqEdit = new QLineEdit(m_controlContainer);
    m_reqEdit->setPlaceholderText(QStringLiteral("e.g. 01 (hex)"));
    controlForm->addRow(QStringLiteral("bRequest"), m_reqEdit);

    m_valEdit = new QLineEdit(m_controlContainer);
    m_valEdit->setPlaceholderText(QStringLiteral("e.g. 0100 (hex)"));
    controlForm->addRow(QStringLiteral("wValue"), m_valEdit);

    m_idxEdit = new QLineEdit(m_controlContainer);
    m_idxEdit->setPlaceholderText(QStringLiteral("e.g. 0000 (hex)"));
    controlForm->addRow(QStringLiteral("wIndex"), m_idxEdit);

    m_controlDataEdit = new QLineEdit(m_controlContainer);
    m_controlDataEdit->setPlaceholderText(QStringLiteral("payload in hex (optional)"));
    controlForm->addRow(QStringLiteral("Payload data"), m_controlDataEdit);

    // Bulk container
    m_bulkContainer = new QWidget(m_stackedWidget);
    auto *bulkForm = new QFormLayout(m_bulkContainer);
    bulkForm->setContentsMargins(0, 0, 0, 0);
    bulkForm->setSpacing(4);

    m_epEdit = new QLineEdit(m_bulkContainer);
    m_epEdit->setPlaceholderText(QStringLiteral("e.g. 02 or 82 (hex)"));
    bulkForm->addRow(QStringLiteral("Endpoint"), m_epEdit);

    m_bulkDataEdit = new QLineEdit(m_bulkContainer);
    m_bulkDataEdit->setPlaceholderText(QStringLiteral("payload in hex"));
    bulkForm->addRow(QStringLiteral("Payload data"), m_bulkDataEdit);

    m_stackedWidget->addWidget(m_controlContainer);
    m_stackedWidget->addWidget(m_bulkContainer);
    form->addRow(m_stackedWidget);

    m_injectBtn = new QPushButton(QStringLiteral("Inject Transfer"), this);
    m_injectBtn->setToolTip(QStringLiteral("Transmit crafted request onto the USB bus"));
    connect(m_injectBtn, &QPushButton::clicked, this, &UsbInjectionPanel::onInjectButtonClicked);
    form->addRow(m_injectBtn);
}

}  // namespace aether
