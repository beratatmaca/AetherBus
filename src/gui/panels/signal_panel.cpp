#include "gui/panels/signal_panel.hpp"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

namespace aether {

SignalPanel::SignalPanel(QWidget *parent) : QGroupBox(QStringLiteral("Signal lines"), parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    m_rtsCheck = new QCheckBox(QStringLiteral("RTS"), this);
    m_dtrCheck = new QCheckBox(QStringLiteral("DTR"), this);
    m_rtsCheck->setToolTip(QStringLiteral("Assert/deassert the RTS output line"));
    m_dtrCheck->setToolTip(QStringLiteral("Assert/deassert the DTR output line"));
    connect(m_rtsCheck, &QCheckBox::toggled, this, &SignalPanel::rtsToggled);
    connect(m_dtrCheck, &QCheckBox::toggled, this, &SignalPanel::dtrToggled);

    auto *breakBtn = new QPushButton(QStringLiteral("Break"), this);
    breakBtn->setToolTip(QStringLiteral("Send a serial BREAK signal"));
    connect(breakBtn, &QPushButton::clicked, this, &SignalPanel::breakTriggered);

    auto *outRow = new QHBoxLayout();
    outRow->setSpacing(6);
    outRow->addWidget(new QLabel(QStringLiteral("Output:"), this));
    outRow->addWidget(m_rtsCheck);
    outRow->addWidget(m_dtrCheck);
    outRow->addWidget(breakBtn);
    outRow->addStretch(1);
    layout->addLayout(outRow);

    m_ctsLed = new QLabel(QStringLiteral("CTS"), this);
    m_dsrLed = new QLabel(QStringLiteral("DSR"), this);
    m_dcdLed = new QLabel(QStringLiteral("DCD"), this);
    m_riLed = new QLabel(QStringLiteral("RI"), this);
    for (QLabel *led : {m_ctsLed, m_dsrLed, m_dcdLed, m_riLed}) {
        led->setStyleSheet(QStringLiteral("color:#555"));
    }

    m_reconnectCheck = new QCheckBox(QStringLiteral("Auto-reconnect"), this);
    m_reconnectCheck->setToolTip(QStringLiteral("Automatically reopen the port if the device disconnects"));

    auto *inRow = new QHBoxLayout();
    inRow->setSpacing(6);
    inRow->addWidget(new QLabel(QStringLiteral("Input:"), this));
    inRow->addWidget(m_ctsLed);
    inRow->addWidget(m_dsrLed);
    inRow->addWidget(m_dcdLed);
    inRow->addWidget(m_riLed);
    inRow->addStretch(1);
    inRow->addWidget(m_reconnectCheck);
    layout->addLayout(inRow);
}

SignalPanel::~SignalPanel() = default;

void SignalPanel::updateModemStatus(bool cts, bool dsr, bool dcd, bool ri) {
    const auto paint = [](QLabel *led, bool on) {
        led->setStyleSheet(on ? QStringLiteral("color:#66bb6a;font-weight:bold") : QStringLiteral("color:#555"));
    };
    paint(m_ctsLed, cts);
    paint(m_dsrLed, dsr);
    paint(m_dcdLed, dcd);
    paint(m_riLed, ri);
}

bool SignalPanel::isAutoReconnectEnabled() const {
    return m_reconnectCheck && m_reconnectCheck->isChecked();
}

}  // namespace aether
