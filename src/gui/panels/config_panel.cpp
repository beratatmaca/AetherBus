#include "gui/panels/config_panel.hpp"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QShortcut>
#include <QSettings>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QStyle>
#include <QApplication>
#include <QTextDocumentFragment>

namespace aether {

ConfigPanel::ConfigPanel(QWidget *parent) : QGroupBox(QStringLiteral("Interception"), parent) {
    auto *form = new QFormLayout(this);
    form->setContentsMargins(6, 6, 6, 6);
    form->setSpacing(4);

    m_deviceBox = new QComboBox(this);
    m_deviceBox->setEditable(true);
    m_deviceBox->setToolTip(QStringLiteral("Select or type the serial device path (e.g. /dev/ttyUSB0)"));

    auto *rescanBtn = new QPushButton(this);
    rescanBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_BrowserReload));
    rescanBtn->setFixedWidth(32);
    rescanBtn->setToolTip(QStringLiteral("Rescan /dev for serial devices (F5)"));
    connect(rescanBtn, &QPushButton::clicked, this, &ConfigPanel::rescanRequested);
    auto *rescanShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(rescanShortcut, &QShortcut::activated, this, &ConfigPanel::rescanRequested);

    auto *deviceRow = new QHBoxLayout();
    deviceRow->addWidget(m_deviceBox, 1);
    deviceRow->addWidget(rescanBtn);
    form->addRow(QStringLiteral("Physical device"), deviceRow);

    m_baudBox = new QComboBox(this);
    m_baudBox->setEditable(true);
    for (const int baud : {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600}) {
        m_baudBox->addItem(QString::number(baud), baud);
    }
    m_baudBox->setCurrentText(QStringLiteral("115200"));
    m_baudBox->setToolTip(QStringLiteral("Set connection baud rate (bps)"));
    form->addRow(QStringLiteral("Baud rate"), m_baudBox);

    m_dataBitsBox = new QComboBox(this);
    for (const int bits : {5, 6, 7, 8}) {
        m_dataBitsBox->addItem(QString::number(bits), bits);
    }
    m_dataBitsBox->setCurrentText(QStringLiteral("8"));
    m_dataBitsBox->setToolTip(QStringLiteral("Select number of data bits (typically 8)"));
    form->addRow(QStringLiteral("Data bits"), m_dataBitsBox);

    m_parityBox = new QComboBox(this);
    m_parityBox->addItem(QStringLiteral("None"), static_cast<int>(Parity::None));
    m_parityBox->addItem(QStringLiteral("Even"), static_cast<int>(Parity::Even));
    m_parityBox->addItem(QStringLiteral("Odd"), static_cast<int>(Parity::Odd));
    m_parityBox->setToolTip(QStringLiteral("Parity checking bit (None, Even, Odd)"));
    form->addRow(QStringLiteral("Parity"), m_parityBox);

    m_stopBitsBox = new QComboBox(this);
    m_stopBitsBox->addItem(QStringLiteral("1"), 1);
    m_stopBitsBox->addItem(QStringLiteral("2"), 2);
    m_stopBitsBox->setToolTip(QStringLiteral("Number of stop bits per frame (typically 1)"));
    form->addRow(QStringLiteral("Stop bits"), m_stopBitsBox);

    m_flowBox = new QComboBox(this);
    m_flowBox->addItem(QStringLiteral("None"), static_cast<int>(FlowControl::None));
    m_flowBox->addItem(QStringLiteral("RTS/CTS"), static_cast<int>(FlowControl::RtsCts));
    m_flowBox->addItem(QStringLiteral("XON/XOFF"), static_cast<int>(FlowControl::XonXoff));
    m_flowBox->setToolTip(QStringLiteral("Hardware or software flow control mode"));
    form->addRow(QStringLiteral("Flow control"), m_flowBox);

    m_symlinkEdit = new QLineEdit(this);
    m_symlinkEdit->setPlaceholderText(QStringLiteral("optional, e.g. ./ttyUSB0_sniffed"));
    m_symlinkEdit->setToolTip(QStringLiteral("Create a custom symlink to point the user application at the PTY slave"));
    form->addRow(QStringLiteral("Slave symlink"), m_symlinkEdit);

    m_directCheck = new QCheckBox(QStringLiteral("Direct Connection (Bypass Proxy)"), this);
    m_directCheck->setToolTip(QStringLiteral("Bypass PTY interceptor and connect directly to serial hardware"));
    form->addRow(m_directCheck);

    m_startButton = new QPushButton(QStringLiteral("Start Interception"), this);
    m_startButton->setToolTip(QStringLiteral("Initialize PTY proxy or connect directly"));
    connect(m_startButton, &QPushButton::clicked, this, &ConfigPanel::onStartButtonClicked);
    form->addRow(m_startButton);

    connect(m_directCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_symlinkEdit->setEnabled(!checked);
        if (checked) {
            m_symlinkEdit->clear();
            m_startButton->setText(QStringLiteral("Connect Direct"));
        } else {
            m_startButton->setText(QStringLiteral("Start Interception"));
        }
    });

    m_statusLabel = new QLabel(QStringLiteral("Idle."), this);
    m_statusLabel->setWordWrap(true);
    form->addRow(QStringLiteral("Status"), m_statusLabel);

    // Load connection settings
    {
        QSettings settings;
        const QString dev = settings.value(QStringLiteral("connection/device")).toString();
        if (!dev.isEmpty()) {
            m_deviceBox->setCurrentText(dev);
        }
        const int baudVal = settings.value(QStringLiteral("connection/baud"), 115200).toInt();
        m_baudBox->setCurrentText(QString::number(baudVal));

        const int dataBitsVal = settings.value(QStringLiteral("connection/dataBits"), 8).toInt();
        m_dataBitsBox->setCurrentText(QString::number(dataBitsVal));

        const QString parityVal = settings.value(QStringLiteral("connection/parity"), QStringLiteral("None")).toString();
        m_parityBox->setCurrentText(parityVal);

        const int stopBitsVal = settings.value(QStringLiteral("connection/stopBits"), 1).toInt();
        m_stopBitsBox->setCurrentText(QString::number(stopBitsVal));

        const QString flowVal = settings.value(QStringLiteral("connection/flow"), QStringLiteral("None")).toString();
        m_flowBox->setCurrentText(flowVal);

        m_symlinkEdit->setText(settings.value(QStringLiteral("connection/symlink")).toString());
        m_directCheck->setChecked(settings.value(QStringLiteral("connection/directMode"), false).toBool());
    }
}

ConfigPanel::~ConfigPanel() = default;

void ConfigPanel::setRunningState(bool running) {
    m_isRunning = running;
    if (m_directCheck->isChecked()) {
        m_startButton->setText(running ? QStringLiteral("Disconnect Direct") : QStringLiteral("Connect Direct"));
    } else {
        m_startButton->setText(running ? QStringLiteral("Stop Interception") : QStringLiteral("Start Interception"));
    }
    const std::initializer_list<QWidget *> controls = {m_deviceBox,   m_baudBox, m_dataBitsBox, m_parityBox,
                                                       m_stopBitsBox, m_flowBox, m_symlinkEdit, m_directCheck};
    for (QWidget *w : controls) {
        if (w) {
            w->setEnabled(!running);
        }
    }
}

void ConfigPanel::setStatus(const QString &htmlText) {
    m_statusLabel->setText(htmlText);
    // Error messages are the ones tagged with the error red; surface the plain
    // text (and that flag) so the main window's status bar can mirror it.
    const bool isError = htmlText.contains(QStringLiteral("#e57373"));
    emit statusChanged(QTextDocumentFragment::fromHtml(htmlText).toPlainText(), isError);
}

void ConfigPanel::populateDevices(const QStringList &systemPorts, const QStringList &byIdPorts) {
    const QString previous = m_deviceBox->currentText();
    m_deviceBox->clear();

    for (const QString &port : systemPorts) {
        m_deviceBox->addItem(port);
    }
    for (const QString &port : byIdPorts) {
        m_deviceBox->addItem(port);
    }

    if (m_deviceBox->count() == 0) {
#if defined(Q_OS_WIN)
        m_deviceBox->addItem(QStringLiteral("COM3"));
#elif defined(Q_OS_MAC)
        m_deviceBox->addItem(QStringLiteral("/dev/tty.usbserial"));
#else
        m_deviceBox->addItem(QStringLiteral("/dev/ttyUSB0"));
#endif
    }

    // Restore recently-used ports from QSettings.
    {
        QSettings settings;
        const QStringList recent = settings.value(QStringLiteral("connection/recentPorts")).toStringList();
        for (const QString &port : recent) {
            if (!port.isEmpty() && m_deviceBox->findText(port) < 0) {
                m_deviceBox->addItem(port);
            }
        }
    }

    if (!previous.isEmpty()) {
        m_deviceBox->setCurrentText(previous);
    }
}

QString ConfigPanel::device() const {
    return m_deviceBox->currentText();
}

void ConfigPanel::onStartButtonClicked() {
    if (m_isRunning) {
        emit stopInterception();
        return;
    }

    const int baud = m_baudBox->currentText().toInt();
    if (baud <= 0) {
        setStatus(QStringLiteral("<span style='color:#e57373'>Invalid baud rate: %1</span>").arg(m_baudBox->currentText()));
        return;
    }

    SerialConfig cfg;
    cfg.device = m_deviceBox->currentText();
    cfg.baud = baud;
    cfg.dataBits = m_dataBitsBox->currentData().toInt();
    cfg.parity = static_cast<Parity>(m_parityBox->currentData().toInt());
    cfg.stopBits = m_stopBitsBox->currentData().toInt();
    cfg.flow = static_cast<FlowControl>(m_flowBox->currentData().toInt());
    cfg.symlinkPath = m_symlinkEdit->text().trimmed();
    cfg.directMode = m_directCheck->isChecked();

    // Persist port in recent-ports list (cap at 10 entries) and configuration
    {
        QSettings settings;
        QStringList recent = settings.value(QStringLiteral("connection/recentPorts")).toStringList();
        recent.removeAll(cfg.device);
        recent.prepend(cfg.device);
        while (recent.size() > 10) {
            recent.removeLast();
        }
        settings.setValue(QStringLiteral("connection/recentPorts"), recent);

        settings.setValue(QStringLiteral("connection/device"), cfg.device);
        settings.setValue(QStringLiteral("connection/baud"), cfg.baud);
        settings.setValue(QStringLiteral("connection/dataBits"), cfg.dataBits);
        settings.setValue(QStringLiteral("connection/parity"), m_parityBox->currentText());
        settings.setValue(QStringLiteral("connection/stopBits"), cfg.stopBits);
        settings.setValue(QStringLiteral("connection/flow"), m_flowBox->currentText());
        settings.setValue(QStringLiteral("connection/symlink"), cfg.symlinkPath);
        settings.setValue(QStringLiteral("connection/directMode"), cfg.directMode);
    }

    emit startInterception(cfg);
}

}  // namespace aether
