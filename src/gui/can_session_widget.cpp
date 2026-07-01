#include "gui/can_session_widget.h"

#include "core/can_backend.h"
#include "gui/can_config_panel.h"
#include "gui/consoleview.h"
#include "gui/statspanel.h"

#include <QByteArray>
#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

namespace aether {

namespace {
/// Parse a hex-byte string ("DE AD BE EF" or "DEADBEEF") into raw bytes.
QByteArray parseHexBytes(const QString &text, bool &ok) {
    QString clean = text;
    clean.remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
    if (clean.size() % 2 != 0) {
        ok = false;
        return {};
    }
    ok = true;
    return QByteArray::fromHex(clean.toLatin1());
}
}  // namespace

CanSessionWidget::CanSessionWidget(QWidget *parent) : SessionView(parent), m_backend(new CanBackend(this)) {
    buildUi();
    rescan();

    connect(m_configPanel, &CanConfigPanel::startCan, this, &CanSessionWidget::startCapture);
    connect(m_configPanel, &CanConfigPanel::stopCan, this, &CanSessionWidget::stopCapture);
    connect(m_configPanel, &CanConfigPanel::rescanRequested, this, &CanSessionWidget::rescan);

    connect(m_backend, &CanBackend::chunkCaptured, m_console, &ConsoleView::appendChunk);
    connect(m_backend, &CanBackend::chunkCaptured, this, &CanSessionWidget::onChunkCaptured);
    connect(m_backend, &CanBackend::started, this, &CanSessionWidget::onStarted);
    connect(m_backend, &CanBackend::stopped, this, &CanSessionWidget::onStopped);
    connect(m_backend, &CanBackend::errorOccurred, this, &CanSessionWidget::onError);
    connect(m_backend, &CanBackend::disconnected, this, &CanSessionWidget::onDisconnected);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, [this] {
        m_stats.rollRates();
        updateCounts();
    });
    m_statsTimer->start();

    if (!CanBackend::isSupported()) {
        m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>SocketCAN is not available on this platform.</span>"));
    }

    m_console->setNewlineMode(ConsoleView::NewlineMode::Frame, 0);
    applyFormats();
}

CanSessionWidget::~CanSessionWidget() {
    if (m_backend) {
        m_backend->disconnect();
        delete m_backend;
        m_backend = nullptr;
    }
}

bool CanSessionWidget::isRunning() const {
    return m_backend && m_backend->isRunning();
}

void CanSessionWidget::stopSession() {
    if (m_backend && m_backend->isRunning()) {
        m_backend->close();
    }
}

void CanSessionWidget::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);

    auto *leftColumn = new QWidget(mainSplitter);
    leftColumn->setMinimumWidth(320);
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    m_configPanel = new CanConfigPanel(leftColumn);
    leftLayout->addWidget(m_configPanel);

    m_statsPanel = new StatsPanel(leftColumn);
    m_statsPanel->setActiveCalculator(&m_stats);
    m_statsPanel->setMinimumHeight(360);
    leftLayout->addWidget(m_statsPanel, 1);

    mainSplitter->addWidget(leftColumn);
    mainSplitter->addWidget(buildConsolePanel(this));
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({340, 1100});

    outer->addWidget(mainSplitter);
}

QWidget *CanSessionWidget::buildConsolePanel(QWidget *parent) {
    auto *panel = new QWidget(parent);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(6);

    m_console = new ConsoleView(panel);

    const auto makeToggle = [&](const QString &text, const QString &tooltip) {
        auto *button = new QPushButton(text, panel);
        button->setCheckable(true);
        button->setToolTip(tooltip);
        button->setProperty("toolbarToggle", true);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    const auto makeAction = [&](const QString &text, const QString &tooltip) {
        auto *button = new QPushButton(text, panel);
        button->setToolTip(tooltip);
        button->setProperty("toolbarAction", true);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    const auto makeDivider = [&]() {
        auto *line = new QFrame(panel);
        line->setFrameShape(QFrame::VLine);
        line->setObjectName(QStringLiteral("toolbarDivider"));
        return line;
    };
    const auto makeSectionLabel = [&](const QString &text) {
        auto *label = new QLabel(text, panel);
        label->setObjectName(QStringLiteral("toolbarSectionLabel"));
        return label;
    };

    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    row->addWidget(makeSectionLabel(QStringLiteral("View")));

    m_hexCheck = makeToggle(QStringLiteral("HEX"), QStringLiteral("Show hexadecimal payload bytes"));
    m_decCheck = makeToggle(QStringLiteral("DEC"), QStringLiteral("Show decimal payload byte values"));
    m_binCheck = makeToggle(QStringLiteral("BIN"), QStringLiteral("Show binary payload byte values"));
    m_asciiCheck = makeToggle(QStringLiteral("ASCII"), QStringLiteral("Show ASCII gutter"));
    m_hexCheck->setChecked(true);
    m_asciiCheck->setChecked(true);
    for (QPushButton *button : {m_hexCheck, m_decCheck, m_binCheck, m_asciiCheck}) {
        button->setProperty("segmentButton", true);
        connect(button, &QPushButton::toggled, this, &CanSessionWidget::applyFormats);
        row->addWidget(button);
    }

    row->addWidget(makeDivider());
    row->addWidget(makeSectionLabel(QStringLiteral("Flow")));
    m_autoScrollCheck = makeToggle(QStringLiteral("Auto"), QStringLiteral("Automatically scroll to the newest frame"));
    m_autoScrollCheck->setChecked(true);
    connect(m_autoScrollCheck, &QPushButton::toggled, m_console, &ConsoleView::setAutoScroll);
    m_pauseCheck = makeToggle(QStringLiteral("Pause"), QStringLiteral("Suspend viewport updates"));
    connect(m_pauseCheck, &QPushButton::toggled, m_console, &ConsoleView::setPaused);
    m_tsCheck = makeToggle(QStringLiteral("Time"), QStringLiteral("Show or hide the timestamp prefix"));
    m_tsCheck->setChecked(true);
    connect(m_tsCheck, &QPushButton::toggled, m_console, &ConsoleView::setShowTimestamps);
    row->addWidget(m_autoScrollCheck);
    row->addWidget(m_pauseCheck);
    row->addWidget(m_tsCheck);

    row->addWidget(makeDivider());
    row->addWidget(makeSectionLabel(QStringLiteral("Actions")));
    auto *clearBtn = makeAction(QStringLiteral("Clear"), QStringLiteral("Clear the frame log"));
    connect(clearBtn, &QPushButton::clicked, m_console, &ConsoleView::clearConsole);
    auto *saveBtn = makeAction(QStringLiteral("Save"), QStringLiteral("Export the visible log to a text file"));
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save CAN log"), QStringLiteral("aetherbus_can.txt"),
                                                          QStringLiteral("Text files (*.txt);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QFile::WriteOnly | QFile::Text)) {
            file.write(m_console->toPlainText().toUtf8());
        } else {
            m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Could not write %1</span>").arg(path));
        }
    });
    row->addWidget(clearBtn);
    row->addWidget(saveBtn);

    row->addWidget(makeDivider());
    m_countsLabel = new QLabel(QStringLiteral("Rx: 0  Tx: 0"), panel);
    m_countsLabel->setObjectName(QStringLiteral("consoleCountsLabel"));
    m_countsLabel->setToolTip(QStringLiteral("Cumulative frame counts. Rates update every second."));
    row->addWidget(m_countsLabel);

    row->addStretch(1);
    layout->addLayout(row);

    layout->addWidget(m_console, 1);

    // --- Transmit bar (cansend-style) ---
    auto *txRow = new QHBoxLayout();
    txRow->setSpacing(6);
    txRow->addWidget(makeSectionLabel(QStringLiteral("Transmit")));

    m_txIdEdit = new QLineEdit(panel);
    m_txIdEdit->setPlaceholderText(QStringLiteral("ID (hex)"));
    m_txIdEdit->setFixedWidth(100);
    m_txIdEdit->setToolTip(QStringLiteral("Frame identifier in hexadecimal (e.g. 123 or 18DAF110)"));
    txRow->addWidget(m_txIdEdit);

    m_txDataEdit = new QLineEdit(panel);
    m_txDataEdit->setPlaceholderText(QStringLiteral("payload hex, e.g. DE AD BE EF"));
    m_txDataEdit->setToolTip(QStringLiteral("Up to 8 bytes (classic) or 64 bytes (CAN-FD)"));
    txRow->addWidget(m_txDataEdit, 1);

    m_txEffCheck = new QCheckBox(QStringLiteral("EFF"), panel);
    m_txEffCheck->setToolTip(QStringLiteral("29-bit extended identifier (auto-enabled for ids > 0x7FF)"));
    m_txRtrCheck = new QCheckBox(QStringLiteral("RTR"), panel);
    m_txRtrCheck->setToolTip(QStringLiteral("Remote-transmission-request frame (no payload)"));
    m_txFdCheck = new QCheckBox(QStringLiteral("FD"), panel);
    m_txFdCheck->setToolTip(QStringLiteral("Transmit as a CAN-FD frame"));
    m_txBrsCheck = new QCheckBox(QStringLiteral("BRS"), panel);
    m_txBrsCheck->setToolTip(QStringLiteral("CAN-FD bit-rate switch"));
    for (QCheckBox *box : {m_txEffCheck, m_txRtrCheck, m_txFdCheck, m_txBrsCheck}) {
        txRow->addWidget(box);
    }

    m_txButton = makeAction(QStringLiteral("Send"), QStringLiteral("Transmit the frame onto the bus"));
    connect(m_txButton, &QPushButton::clicked, this, &CanSessionWidget::transmit);
    connect(m_txDataEdit, &QLineEdit::returnPressed, this, &CanSessionWidget::transmit);
    connect(m_txIdEdit, &QLineEdit::returnPressed, this, &CanSessionWidget::transmit);
    txRow->addWidget(m_txButton);

    layout->addLayout(txRow);

    connect(m_console, &ConsoleView::countsChanged, this, [this](qint64, qint64, qint64, qint64) { updateCounts(); });

    return panel;
}

void CanSessionWidget::rescan() {
    m_configPanel->populateInterfaces(CanBackend::listInterfaces());
    m_configPanel->setStatus(QStringLiteral("Interface list refreshed."));
}

void CanSessionWidget::startCapture(const CanConfig &cfg) {
    m_backend->open(cfg);
}

void CanSessionWidget::stopCapture() {
    m_backend->close();
}

void CanSessionWidget::onStarted(const QString &info) {
    m_configPanel->setRunningState(true);
    m_configPanel->setStatus(QStringLiteral("Capturing on <b>%1</b>").arg(info));
    emit sessionTitleChanged(QStringLiteral("[CAN] %1").arg(m_configPanel->iface()));
}

void CanSessionWidget::onStopped() {
    m_configPanel->setRunningState(false);
    m_configPanel->setStatus(QStringLiteral("Stopped."));
    emit sessionTitleChanged(QStringLiteral("CAN Session"));
}

void CanSessionWidget::onError(const QString &message) {
    m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>%1</span>").arg(message));
}

void CanSessionWidget::onDisconnected() {
    m_configPanel->setRunningState(false);
    m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Interface lost.</span>"));
    emit sessionTitleChanged(QStringLiteral("CAN Session"));
}

void CanSessionWidget::onChunkCaptured(const aether::CapturedChunk &chunk) {
    m_stats.addChunk(chunk);
}

void CanSessionWidget::transmit() {
    bool okId = false;
    const quint32 id = m_txIdEdit->text().trimmed().toUInt(&okId, 16);
    if (!okId) {
        m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Invalid transmit id.</span>"));
        return;
    }

    bool okData = false;
    const QByteArray payload = parseHexBytes(m_txDataEdit->text(), okData);
    if (!okData) {
        m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Payload must be whole hex bytes.</span>"));
        return;
    }

    quint16 flags = 0;
    if (m_txEffCheck->isChecked() || id > 0x7FF) {
        flags |= FrameExtendedId;
    }
    if (m_txRtrCheck->isChecked()) {
        flags |= FrameRemote;
    }
    if (m_txFdCheck->isChecked()) {
        flags |= FrameFd;
    }
    if (m_txBrsCheck->isChecked()) {
        flags |= FrameBitRateSwitch;
    }

    if (!m_backend->sendFrame(id, flags, payload)) {
        m_configPanel->setStatus(m_backend->isRunning()
                                     ? QStringLiteral("<span style='color:#e57373'>Transmit failed (payload too large?).</span>")
                                     : QStringLiteral("<span style='color:#e57373'>Start capture before transmitting.</span>"));
    }
}

void CanSessionWidget::applyFormats() {
    m_console->setFormats(m_hexCheck->isChecked(), m_decCheck->isChecked(), m_binCheck->isChecked(), m_asciiCheck->isChecked());
}

void CanSessionWidget::updateCounts() {
    const auto fmtRate = [](double bytesPerSec) -> QString {
        if (bytesPerSec >= 1024.0) {
            return QStringLiteral("%1 KB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
        }
        return QStringLiteral("%1 B/s").arg(static_cast<qint64>(bytesPerSec));
    };
    const QString text = QStringLiteral("Rx: %1 frm (%2)  Tx: %3 frm (%4)")
                             .arg(m_stats.rxChunks())
                             .arg(fmtRate(m_stats.currentRxRate()))
                             .arg(m_stats.txChunks())
                             .arg(fmtRate(m_stats.currentTxRate()));
    m_countsLabel->setText(text);
}

}  // namespace aether
