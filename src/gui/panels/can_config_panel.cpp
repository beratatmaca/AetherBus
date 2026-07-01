#include "gui/panels/can_config_panel.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QShortcut>
#include <QStyle>

namespace aether {

namespace {
constexpr quint32 kStdIdMask = 0x7FF;
constexpr quint32 kExtIdMask = 0x1FFFFFFF;

/// Strip a trailing 'x'/'X' extended-id marker; returns true if one was present.
bool stripExtendedMarker(QString &token) {
    if (token.endsWith(QLatin1Char('x'), Qt::CaseInsensitive)) {
        token.chop(1);
        return true;
    }
    return false;
}
}  // namespace

CanConfigPanel::CanConfigPanel(QWidget *parent) : QGroupBox(QStringLiteral("CAN Connection"), parent) {
    auto *form = new QFormLayout(this);
    form->setContentsMargins(6, 6, 6, 6);
    form->setSpacing(4);

    m_ifaceBox = new QComboBox(this);
    m_ifaceBox->setEditable(true);
    m_ifaceBox->setToolTip(
        QStringLiteral("Select or type the CAN interface (e.g. can0, vcan0).\n"
                       "Configure its bit rate externally: ip link set can0 type can bitrate 500000"));

    auto *rescanBtn = new QPushButton(this);
    rescanBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_BrowserReload));
    rescanBtn->setFixedWidth(32);
    rescanBtn->setToolTip(QStringLiteral("Rescan for CAN interfaces (F5)"));
    connect(rescanBtn, &QPushButton::clicked, this, &CanConfigPanel::rescanRequested);
    auto *rescanShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(rescanShortcut, &QShortcut::activated, this, &CanConfigPanel::rescanRequested);

    auto *ifaceRow = new QHBoxLayout();
    ifaceRow->addWidget(m_ifaceBox, 1);
    ifaceRow->addWidget(rescanBtn);
    form->addRow(QStringLiteral("Interface"), ifaceRow);

    m_fdCheck = new QCheckBox(QStringLiteral("CAN-FD (up to 64 bytes)"), this);
    m_fdCheck->setChecked(true);
    m_fdCheck->setToolTip(QStringLiteral("Enable flexible-data-rate frames (CAN_RAW_FD_FRAMES)"));
    form->addRow(m_fdCheck);

    m_loopbackCheck = new QCheckBox(QStringLiteral("Local loopback"), this);
    m_loopbackCheck->setChecked(true);
    m_loopbackCheck->setToolTip(QStringLiteral("Deliver locally sent frames to other sockets on this host"));
    form->addRow(m_loopbackCheck);

    m_recvOwnCheck = new QCheckBox(QStringLiteral("Receive own frames"), this);
    m_recvOwnCheck->setToolTip(QStringLiteral("Also receive frames this session transmits (CAN_RAW_RECV_OWN_MSGS)"));
    form->addRow(m_recvOwnCheck);

    m_errorCheck = new QCheckBox(QStringLiteral("Show error frames"), this);
    m_errorCheck->setChecked(true);
    m_errorCheck->setToolTip(QStringLiteral("Subscribe to bus error frames (CAN_RAW_ERR_FILTER)"));
    form->addRow(m_errorCheck);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(QStringLiteral("all frames — e.g. 123, 7DF/7FF, 18DAF110x"));
    m_filterEdit->setToolTip(
        QStringLiteral("Receive filters (comma/space separated). Blank = accept all.\n"
                       "  ID           match id with default mask\n"
                       "  ID/MASK      match id under an explicit hex mask\n"
                       "  suffix 'x'   treat id as 29-bit extended (e.g. 18DAF110x)\n"
                       "All values are hexadecimal."));
    form->addRow(QStringLiteral("Filters"), m_filterEdit);

    m_startButton = new QPushButton(QStringLiteral("Start Capture"), this);
    m_startButton->setToolTip(QStringLiteral("Open the CAN socket and begin capturing"));
    connect(m_startButton, &QPushButton::clicked, this, &CanConfigPanel::onStartButtonClicked);
    form->addRow(m_startButton);

    m_statusLabel = new QLabel(QStringLiteral("Idle."), this);
    m_statusLabel->setWordWrap(true);
    form->addRow(QStringLiteral("Status"), m_statusLabel);

    {
        QSettings settings;
        const QString iface = settings.value(QStringLiteral("can/iface")).toString();
        if (!iface.isEmpty()) {
            m_ifaceBox->setCurrentText(iface);
        }
        m_filterEdit->setText(settings.value(QStringLiteral("can/filters")).toString());
    }
}

CanConfigPanel::~CanConfigPanel() = default;

void CanConfigPanel::setRunningState(bool running) {
    m_isRunning = running;
    m_startButton->setText(running ? QStringLiteral("Stop Capture") : QStringLiteral("Start Capture"));
    const std::initializer_list<QWidget *> controls = {m_ifaceBox, m_fdCheck, m_loopbackCheck, m_recvOwnCheck, m_errorCheck, m_filterEdit};
    for (QWidget *w : controls) {
        if (w) {
            w->setEnabled(!running);
        }
    }
}

void CanConfigPanel::setStatus(const QString &htmlText) {
    m_statusLabel->setText(htmlText);
}

void CanConfigPanel::populateInterfaces(const QStringList &ifaces) {
    const QString previous = m_ifaceBox->currentText();
    m_ifaceBox->clear();
    m_ifaceBox->addItems(ifaces);
    if (m_ifaceBox->count() == 0) {
        m_ifaceBox->addItem(QStringLiteral("can0"));
        m_ifaceBox->addItem(QStringLiteral("vcan0"));
    }
    if (!previous.isEmpty()) {
        m_ifaceBox->setCurrentText(previous);
    }
}

QString CanConfigPanel::iface() const {
    return m_ifaceBox->currentText();
}

bool CanConfigPanel::buildConfig(CanConfig &out) {
    out.iface = m_ifaceBox->currentText().trimmed();
    out.fdMode = m_fdCheck->isChecked();
    out.loopback = m_loopbackCheck->isChecked();
    out.receiveOwn = m_recvOwnCheck->isChecked();
    out.errorFrames = m_errorCheck->isChecked();
    out.filters.clear();

    const QString spec = m_filterEdit->text().trimmed();
    if (spec.isEmpty()) {
        return true;
    }

    const QStringList tokens = spec.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    for (const QString &raw : tokens) {
        CanFilter f;
        QString idPart = raw;
        QString maskPart;
        if (const int slash = raw.indexOf(QLatin1Char('/')); slash >= 0) {
            idPart = raw.left(slash);
            maskPart = raw.mid(slash + 1);
        }

        const bool extended = stripExtendedMarker(idPart);
        bool okId = false;
        f.id = idPart.toUInt(&okId, 16);
        if (!okId) {
            setStatus(QStringLiteral("<span style='color:#e57373'>Bad filter id '%1'</span>").arg(raw));
            return false;
        }
        f.extended = extended;

        if (!maskPart.isEmpty()) {
            stripExtendedMarker(maskPart);
            bool okMask = false;
            f.mask = maskPart.toUInt(&okMask, 16);
            if (!okMask) {
                setStatus(QStringLiteral("<span style='color:#e57373'>Bad filter mask '%1'</span>").arg(raw));
                return false;
            }
        } else {
            f.mask = extended ? kExtIdMask : kStdIdMask;
        }
        out.filters.push_back(f);
    }
    return true;
}

void CanConfigPanel::onStartButtonClicked() {
    if (m_isRunning) {
        emit stopCan();
        return;
    }

    CanConfig cfg;
    if (!buildConfig(cfg)) {
        return;
    }
    if (const QString problem = cfg.validate(); !problem.isEmpty()) {
        setStatus(QStringLiteral("<span style='color:#e57373'>%1</span>").arg(problem));
        return;
    }

    {
        QSettings settings;
        settings.setValue(QStringLiteral("can/iface"), cfg.iface);
        settings.setValue(QStringLiteral("can/filters"), m_filterEdit->text().trimmed());
    }

    emit startCan(cfg);
}

}  // namespace aether
