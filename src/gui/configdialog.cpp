#include "configdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QMessageBox>

ConfigDialog::ConfigDialog(const QString& interfaceName, const QString& interfaceType, QWidget *parent)
    : QDialog(parent), m_interfaceName(interfaceName), m_interfaceType(interfaceType) {
    
    setWindowTitle(QString("Configure: %1").arg(interfaceName));
    setMinimumWidth(320);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    QLabel* titleLabel = new QLabel(QString("Configure Parameters for %1 (%2)").arg(interfaceName, interfaceType), this);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    mainLayout->addWidget(titleLabel);

    QWidget* configWidget = new QWidget(this);
    if (interfaceType == "CAN") {
        setupCanLayout(configWidget);
    } else if (interfaceType == "Serial") {
        setupSerialLayout(configWidget);
    } else if (interfaceType == "Ethernet") {
        setupEthernetLayout(configWidget);
    } else {
        setupMockLayout(configWidget);
    }
    mainLayout->addWidget(configWidget);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* okButton = new QPushButton("Apply Settings", this);
    QPushButton* cancelButton = new QPushButton("Cancel", this);
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);

    connect(okButton, &QPushButton::clicked, this, &ConfigDialog::onAccepted);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

QVariantMap ConfigDialog::getConfiguration() const {
    return m_configResult;
}

void ConfigDialog::setupCanLayout(QWidget* container) {
    QFormLayout* layout = new QFormLayout(container);
    m_canBaud = new QComboBox(container);
    m_canBaud->addItems({"250000", "500000", "1000000"});
    m_canBaud->setCurrentText("500000");

    m_canFd = new QComboBox(container);
    m_canFd->addItems({"Enabled", "Disabled"});
    m_canFd->setCurrentText("Disabled");

    layout->addRow("Arbitration Bitrate (bps):", m_canBaud);
    layout->addRow("CAN-FD Mode:", m_canFd);
}

void ConfigDialog::setupSerialLayout(QWidget* container) {
    QFormLayout* layout = new QFormLayout(container);
    
    m_serialBaud = new QComboBox(container);
    m_serialBaud->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_serialBaud->setCurrentText("115200");

    m_serialDataBits = new QComboBox(container);
    m_serialDataBits->addItems({"8", "7"});
    m_serialDataBits->setCurrentText("8");

    m_serialParity = new QComboBox(container);
    m_serialParity->addItems({"None", "Even", "Odd"});
    m_serialParity->setCurrentText("None");

    m_serialStopBits = new QComboBox(container);
    m_serialStopBits->addItems({"1", "2"});
    m_serialStopBits->setCurrentText("1");

    layout->addRow("Baud Rate:", m_serialBaud);
    layout->addRow("Data Bits:", m_serialDataBits);
    layout->addRow("Parity:", m_serialParity);
    layout->addRow("Stop Bits:", m_serialStopBits);
}

void ConfigDialog::setupEthernetLayout(QWidget* container) {
    QFormLayout* layout = new QFormLayout(container);

    m_ethDestIp = new QLineEdit("127.0.0.1", container);
    m_ethPort = new QLineEdit("9870", container);
    
    m_ethProto = new QComboBox(container);
    m_ethProto->addItems({"UDP", "TCP Client", "Raw L2"});
    m_ethProto->setCurrentText("UDP");

    layout->addRow("Destination Address:", m_ethDestIp);
    layout->addRow("Port:", m_ethPort);
    layout->addRow("Protocol Mode:", m_ethProto);
}

void ConfigDialog::setupMockLayout(QWidget* container) {
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->addWidget(new QLabel("No specific settings required. Simulation runs automatically.", container));
}

void ConfigDialog::onAccepted() {
    m_configResult["interface"] = m_interfaceName;
    m_configResult["type"] = m_interfaceType;

    if (m_interfaceType == "CAN") {
        m_configResult["bitrate"] = m_canBaud->currentText().toInt();
        m_configResult["fd"] = (m_canFd->currentText() == "Enabled");
    } else if (m_interfaceType == "Serial") {
        m_configResult["baudrate"] = m_serialBaud->currentText().toInt();
        m_configResult["databits"] = m_serialDataBits->currentText().toInt();
        m_configResult["parity"] = m_serialParity->currentText();
        m_configResult["stopbits"] = m_serialStopBits->currentText().toInt();
    } else if (m_interfaceType == "Ethernet") {
        bool ok;
        int port = m_ethPort->text().toInt(&ok);
        if (!ok || port <= 0 || port > 65535) {
            QMessageBox::warning(this, "Configuration Error", "Please specify a valid port number.");
            return;
        }
        m_configResult["destination"] = m_ethDestIp->text();
        m_configResult["port"] = port;
        m_configResult["protocol"] = m_ethProto->currentText();
    }
    accept();
}
