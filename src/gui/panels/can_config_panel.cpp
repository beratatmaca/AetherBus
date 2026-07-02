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
#include <QTableWidget>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

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

    // Filters Section
    m_filterTable = new QTableWidget(this);
    m_filterTable->setColumnCount(5);
    m_filterTable->setHorizontalHeaderLabels(
        {QStringLiteral("Use"), QStringLiteral("ID (Hex)"), QStringLiteral("Mask (Hex)"), QStringLiteral("Ext"), QStringLiteral("Invert")});
    m_filterTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_filterTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_filterTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_filterTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_filterTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_filterTable->verticalHeader()->setVisible(false);
    m_filterTable->setFixedHeight(120);
    m_filterTable->setToolTip(QStringLiteral("Dynamic receive filter rules. Blank ID/Mask or inactive rules are ignored."));

    auto *filterButtonsLayout = new QHBoxLayout();
    m_addFilterBtn = new QPushButton(QStringLiteral("+ Add"), this);
    m_addFilterBtn->setToolTip(QStringLiteral("Add a new filter rule"));
    connect(m_addFilterBtn, &QPushButton::clicked, this,
            [this] { addFilterRow(true, QStringLiteral("000"), QStringLiteral("7FF"), false, false); });

    m_removeFilterBtn = new QPushButton(QStringLiteral("- Del"), this);
    m_removeFilterBtn->setToolTip(QStringLiteral("Remove selected filter rule"));
    connect(m_removeFilterBtn, &QPushButton::clicked, this, [this] {
        int r = m_filterTable->currentRow();
        if (r >= 0) {
            m_filterTable->removeRow(r);
        }
    });

    m_clearFiltersBtn = new QPushButton(QStringLiteral("Clear"), this);
    m_clearFiltersBtn->setToolTip(QStringLiteral("Remove all filter rules"));
    connect(m_clearFiltersBtn, &QPushButton::clicked, this, [this] { m_filterTable->setRowCount(0); });

    filterButtonsLayout->addWidget(m_addFilterBtn);
    filterButtonsLayout->addWidget(m_removeFilterBtn);
    filterButtonsLayout->addWidget(m_clearFiltersBtn);

    auto *filterContainer = new QWidget(this);
    auto *filterLayout = new QVBoxLayout(filterContainer);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(2);
    filterLayout->addWidget(m_filterTable);
    filterLayout->addLayout(filterButtonsLayout);

    form->addRow(QStringLiteral("Filters"), filterContainer);

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
        loadFiltersFromString(settings.value(QStringLiteral("can/filters")).toString());
    }
}

CanConfigPanel::~CanConfigPanel() = default;

void CanConfigPanel::setRunningState(bool running) {
    m_isRunning = running;
    m_startButton->setText(running ? QStringLiteral("Stop Capture") : QStringLiteral("Start Capture"));
    const std::initializer_list<QWidget *> controls = {m_ifaceBox,    m_fdCheck,      m_loopbackCheck,   m_recvOwnCheck,   m_errorCheck,
                                                       m_filterTable, m_addFilterBtn, m_removeFilterBtn, m_clearFiltersBtn};
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

    for (int i = 0; i < m_filterTable->rowCount(); ++i) {
        bool use = (m_filterTable->item(i, 0)->checkState() == Qt::Checked);
        if (!use) {
            continue;
        }

        QString idPart = m_filterTable->item(i, 1)->text().trimmed();
        QString maskPart = m_filterTable->item(i, 2)->text().trimmed();
        bool extended = (m_filterTable->item(i, 3)->checkState() == Qt::Checked);
        bool invert = (m_filterTable->item(i, 4)->checkState() == Qt::Checked);

        if (idPart.isEmpty()) {
            setStatus(QStringLiteral("<span style='color:#e57373'>Empty filter ID at row %1</span>").arg(i + 1));
            return false;
        }

        CanFilter f;
        bool okId = false;
        f.id = idPart.toUInt(&okId, 16);
        if (!okId) {
            setStatus(QStringLiteral("<span style='color:#e57373'>Bad filter id '%1' at row %2</span>").arg(idPart).arg(i + 1));
            return false;
        }
        f.extended = extended;
        f.invert = invert;

        if (!maskPart.isEmpty()) {
            bool okMask = false;
            f.mask = maskPart.toUInt(&okMask, 16);
            if (!okMask) {
                setStatus(QStringLiteral("<span style='color:#e57373'>Bad filter mask '%1' at row %2</span>").arg(maskPart).arg(i + 1));
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
        settings.setValue(QStringLiteral("can/filters"), saveFiltersToString());
    }

    emit startCan(cfg);
}

void CanConfigPanel::addFilterRow(bool use, const QString &idHex, const QString &maskHex, bool ext, bool invert) {
    int row = m_filterTable->rowCount();
    m_filterTable->insertRow(row);

    auto *useItem = new QTableWidgetItem();
    useItem->setCheckState(use ? Qt::Checked : Qt::Unchecked);
    useItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    useItem->setTextAlignment(Qt::AlignCenter);
    m_filterTable->setItem(row, 0, useItem);

    auto *idItem = new QTableWidgetItem(idHex);
    idItem->setTextAlignment(Qt::AlignCenter);
    m_filterTable->setItem(row, 1, idItem);

    auto *maskItem = new QTableWidgetItem(maskHex);
    maskItem->setTextAlignment(Qt::AlignCenter);
    m_filterTable->setItem(row, 2, maskItem);

    auto *extItem = new QTableWidgetItem();
    extItem->setCheckState(ext ? Qt::Checked : Qt::Unchecked);
    extItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    extItem->setTextAlignment(Qt::AlignCenter);
    m_filterTable->setItem(row, 3, extItem);

    auto *invertItem = new QTableWidgetItem();
    invertItem->setCheckState(invert ? Qt::Checked : Qt::Unchecked);
    invertItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    invertItem->setTextAlignment(Qt::AlignCenter);
    m_filterTable->setItem(row, 4, invertItem);
}

void CanConfigPanel::loadFiltersFromString(const QString &spec) {
    m_filterTable->setRowCount(0);
    if (spec.isEmpty()) {
        return;
    }

    // Try parsing as JSON first
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(spec.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isArray()) {
        QJsonArray arr = doc.array();
        for (auto &&i : arr) {
            QJsonObject obj = i.toObject();
            bool use = obj.value(QStringLiteral("use")).toBool(true);
            QString id = obj.value(QStringLiteral("id")).toString();
            QString mask = obj.value(QStringLiteral("mask")).toString();
            bool ext = obj.value(QStringLiteral("ext")).toBool(false);
            bool invert = obj.value(QStringLiteral("invert")).toBool(false);
            addFilterRow(use, id, mask, ext, invert);
        }
        return;
    }

    // Fallback: legacy space/comma separated parsing
    const QStringList tokens = spec.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    for (const QString &raw : tokens) {
        QString idPart = raw;
        QString maskPart;
        if (const int slash = raw.indexOf(QLatin1Char('/')); slash >= 0) {
            idPart = raw.left(slash);
            maskPart = raw.mid(slash + 1);
        }

        const bool extended = idPart.endsWith(QLatin1Char('x'), Qt::CaseInsensitive) || idPart.endsWith(QLatin1Char('X'));
        if (extended) {
            idPart.chop(1);
        }

        if (maskPart.isEmpty()) {
            maskPart = extended ? QStringLiteral("1FFFFFFF") : QStringLiteral("7FF");
        } else {
            if (maskPart.endsWith(QLatin1Char('x'), Qt::CaseInsensitive) || maskPart.endsWith(QLatin1Char('X'))) {
                maskPart.chop(1);
            }
        }
        addFilterRow(true, idPart, maskPart, extended, false);
    }
}

QString CanConfigPanel::saveFiltersToString() const {
    QJsonArray arr;
    for (int i = 0; i < m_filterTable->rowCount(); ++i) {
        QJsonObject obj;
        obj[QStringLiteral("use")] = (m_filterTable->item(i, 0)->checkState() == Qt::Checked);
        obj[QStringLiteral("id")] = m_filterTable->item(i, 1)->text().trimmed();
        obj[QStringLiteral("mask")] = m_filterTable->item(i, 2)->text().trimmed();
        obj[QStringLiteral("ext")] = (m_filterTable->item(i, 3)->checkState() == Qt::Checked);
        obj[QStringLiteral("invert")] = (m_filterTable->item(i, 4)->checkState() == Qt::Checked);
        arr.append(obj);
    }
    QJsonDocument doc(arr);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

}  // namespace aether
