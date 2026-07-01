#include "gui/injection_panel.h"
#include "core/format_codec.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QTimer>

namespace aether {

InjectionPanel::InjectionPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *injectRow = new QHBoxLayout();
    m_injectFormatBox = new QComboBox(this);
    m_injectFormatBox->addItem(QStringLiteral("HEX"));
    m_injectFormatBox->addItem(QStringLiteral("ASCII"));
    m_injectFormatBox->addItem(QStringLiteral("DEC"));
    m_injectFormatBox->addItem(QStringLiteral("BIN"));
    m_injectFormatBox->setToolTip(QStringLiteral("Interpretation format of injection input text"));

    m_injectEdit = new QLineEdit(this);
    m_injectEdit->setPlaceholderText(QStringLiteral("bytes to inject (e.g. 41 42 0D 0A, or text)"));
    m_injectEdit->setToolTip(QStringLiteral("Enter payload command data to send"));

    m_injectEndingBox = new QComboBox(this);
    m_injectEndingBox->addItem(QStringLiteral("No ending"));
    m_injectEndingBox->addItem(QStringLiteral("CR"));
    m_injectEndingBox->addItem(QStringLiteral("LF"));
    m_injectEndingBox->addItem(QStringLiteral("CR+LF"));
    m_injectEndingBox->setToolTip(QStringLiteral("Suffix character automatically appended to input"));

    m_toDeviceBtn = new QPushButton(QStringLiteral("Send → Device"), this);
    m_toDeviceBtn->setToolTip(QStringLiteral("Inject data directly to physical serial port (Tx)"));

    m_toAppBtn = new QPushButton(QStringLiteral("Send → App"), this);
    m_toAppBtn->setToolTip(QStringLiteral("Inject data to target virtual PTY interface (Rx)"));

    m_fileBtn = new QPushButton(QStringLiteral("File…"), this);
    m_fileBtn->setToolTip(QStringLiteral("Select a file and send all bytes to the device"));

    m_saveMacroBtn = new QPushButton(QStringLiteral("★ Save as macro"), this);
    m_saveMacroBtn->setToolTip(QStringLiteral("Save the current input as a reusable one-click macro"));

    connect(m_toDeviceBtn, &QPushButton::clicked, this, [this] { sendInjection(true); });
    connect(m_toAppBtn, &QPushButton::clicked, this, [this] { sendInjection(false); });
    connect(m_fileBtn, &QPushButton::clicked, this, &InjectionPanel::onSendFileClicked);
    connect(m_injectEdit, &QLineEdit::returnPressed, this, [this] { sendInjection(true); });
    connect(m_saveMacroBtn, &QPushButton::clicked, this, [this] {
        emit saveAsMacroRequested(m_injectFormatBox->currentIndex(), m_injectEdit->text(), m_injectEndingBox->currentIndex(),
                                  m_repeatToDevice);
    });

    injectRow->addWidget(m_injectFormatBox);
    injectRow->addWidget(m_injectEdit, 1);
    injectRow->addWidget(m_injectEndingBox);
    injectRow->addWidget(m_toDeviceBtn);
    injectRow->addWidget(m_toAppBtn);
    injectRow->addWidget(m_fileBtn);
    injectRow->addWidget(m_saveMacroBtn);
    layout->addLayout(injectRow);

    auto *repeatRow = new QHBoxLayout();
    m_repeatCheck = new QCheckBox(QStringLiteral("Repeat send every"), this);
    m_repeatCheck->setToolTip(QStringLiteral("Check to enable auto-injection repeat timer"));

    m_repeatIntervalEdit = new QLineEdit(QStringLiteral("1000"), this);
    m_repeatIntervalEdit->setFixedWidth(72);
    m_repeatIntervalEdit->setToolTip(QStringLiteral("Transmission repeat period (ms)"));

    m_repeatTimer = new QTimer(this);
    connect(m_repeatTimer, &QTimer::timeout, this, [this] { sendInjection(m_repeatToDevice); });
    connect(m_repeatCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            const int ms = qMax(10, m_repeatIntervalEdit->text().toInt());
            m_repeatTimer->start(ms);
        } else {
            m_repeatTimer->stop();
        }
    });

    repeatRow->addWidget(m_repeatCheck);
    repeatRow->addWidget(m_repeatIntervalEdit);
    repeatRow->addWidget(new QLabel(QStringLiteral("ms (repeats the last Send direction)"), this));
    repeatRow->addStretch(1);
    layout->addLayout(repeatRow);
}

InjectionPanel::~InjectionPanel() = default;

void InjectionPanel::setRunningState(bool running, bool directMode) {
    if (running) {
        m_toAppBtn->setEnabled(!directMode);
    } else {
        m_toAppBtn->setEnabled(true);
    }
}

QByteArray InjectionPanel::encodeInjection(bool &ok) {
    QByteArray bytes;
    QString error;
    ok = codec::encodePayload(m_injectFormatBox->currentIndex(), m_injectEdit->text(), m_injectEndingBox->currentIndex(), bytes, &error);
    if (!ok) {
        emit injectionError(error);
        return {};
    }
    return bytes;
}

void InjectionPanel::sendInjection(bool toDevice) {
    m_repeatToDevice = toDevice;
    bool ok = false;
    const QByteArray bytes = encodeInjection(ok);
    if (!ok || bytes.isEmpty()) {
        return;
    }
    emit injectData(bytes, toDevice);
}

void InjectionPanel::onSendFileClicked() {
    emit fileSendRequested();
}

}  // namespace aether
