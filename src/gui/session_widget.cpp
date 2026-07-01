#include "gui/session_widget.h"

#include "core/capture_replay.h"
#include "core/format_codec.h"
#include "core/pty_proxy.h"
#include "gui/consoleview.h"
#include "gui/macrobar.h"
#include "gui/statspanel.h"
#include "gui/config_panel.h"
#include "gui/signal_panel.h"
#include "gui/injection_panel.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QTextDocument>
#include <QTextCursor>

namespace aether {

SessionWidget::SessionWidget(QWidget *parent) : QWidget(parent), m_proxy(new PtyProxy(this)) {
    buildUi();
    rescanDevices();

    // Wire Up Panels
    connect(m_configPanel, &ConfigPanel::startInterception, this, &SessionWidget::startInterception);
    connect(m_configPanel, &ConfigPanel::stopInterception, this, &SessionWidget::stopInterception);
    connect(m_configPanel, &ConfigPanel::rescanRequested, this, &SessionWidget::rescanDevices);

    connect(m_signalPanel, &SignalPanel::rtsToggled, this, [this](bool on) { (void)m_proxy->setRts(on); });
    connect(m_signalPanel, &SignalPanel::dtrToggled, this, [this](bool on) { (void)m_proxy->setDtr(on); });
    connect(m_signalPanel, &SignalPanel::breakTriggered, this, [this] { (void)m_proxy->sendBreak(); });

    connect(m_injectPanel, &InjectionPanel::injectData, this, [this](const QByteArray &data, bool toDevice) {
        const bool sent = toDevice ? m_proxy->injectToDevice(data) : m_proxy->injectToApp(data);
        if (!sent) {
            onError(m_proxy->isRunning() ? QStringLiteral("Send failed — send buffer full, bytes dropped.")
                                         : QStringLiteral("Send failed — start interception first."));
            return;
        }
        if (m_macroBar) {
            m_macroBar->pushHistory(data, toDevice);
        }
    });
    connect(m_injectPanel, &InjectionPanel::injectionError, this, &SessionWidget::onError);
    connect(m_injectPanel, &InjectionPanel::fileSendRequested, this, &SessionWidget::sendFile);

    // Proxy connections
    connect(m_proxy, &PtyProxy::chunkCaptured, m_console, &ConsoleView::appendChunk);
    connect(m_proxy, &PtyProxy::chunkCaptured, this, &SessionWidget::onChunkCaptured);
    connect(m_proxy, &PtyProxy::started, this, &SessionWidget::onStarted);
    connect(m_proxy, &PtyProxy::stopped, this, &SessionWidget::onStopped);
    connect(m_proxy, &PtyProxy::errorOccurred, this, &SessionWidget::onError);
    connect(m_proxy, &PtyProxy::disconnected, this, &SessionWidget::onDisconnected);
    connect(m_proxy, &PtyProxy::lineReconfigured, this, &SessionWidget::onLineReconfigured);
    connect(m_proxy, &PtyProxy::writeStalled, this, &SessionWidget::onWriteStalled);

    // Offline replay feeds chunks through the same rendering path as live capture.
    m_replayer = new CaptureReplayer(this);
    connect(m_replayer, &CaptureReplayer::chunkReplayed, m_console, &ConsoleView::appendChunk);
    connect(m_replayer, &CaptureReplayer::finished, this, &SessionWidget::onReplayFinished);

    m_modemTimer = new QTimer(this);
    m_modemTimer->setInterval(250);
    connect(m_modemTimer, &QTimer::timeout, this, &SessionWidget::pollModemLines);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(1000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_proxy->isRunning()) {
            m_reconnectTimer->stop();
            return;
        }
        if (m_proxy->open(m_lastConfig)) {
            m_reconnectTimer->stop();
        }
    });

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, [this] { m_stats.rollRates(); });
    m_statsTimer->start();

    // Configure initial stats calculator parameters
    QSettings settings;
    int baudVal = settings.value(QStringLiteral("connection/baud"), 115200).toInt();
    int dataVal = settings.value(QStringLiteral("connection/dataBits"), 8).toInt();
    QString parityText = settings.value(QStringLiteral("connection/parity"), QStringLiteral("None")).toString();
    Parity parityVal = Parity::None;
    if (parityText == QStringLiteral("Even"))
        parityVal = Parity::Even;
    else if (parityText == QStringLiteral("Odd"))
        parityVal = Parity::Odd;
    int stopVal = settings.value(QStringLiteral("connection/stopBits"), 1).toInt();
    m_stats.setSerialConfig(baudVal, dataVal, parityVal, stopVal);

    applyFormats();
    applyNewlineMode();
}

SessionWidget::~SessionWidget() {
    if (m_proxy) {
        m_proxy->disconnect();
        delete m_proxy;
        m_proxy = nullptr;
    }
}

bool SessionWidget::isRunning() const {
    return m_proxy && m_proxy->isRunning();
}

void SessionWidget::stopSession() {
    if (m_proxy && m_proxy->isRunning()) {
        m_proxy->close();
    }
}

void SessionWidget::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);

    // Left column: config, signal lines and stats stacked tightly in a plain
    // layout (no inner splitter), so the panels stay compact at the top rather
    // than floating apart. Stats takes the remaining vertical space.
    auto *leftColumn = new QWidget(mainSplitter);
    leftColumn->setMinimumWidth(320);
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    m_configPanel = new ConfigPanel(leftColumn);
    leftLayout->addWidget(m_configPanel);

    m_signalPanel = new SignalPanel(leftColumn);
    leftLayout->addWidget(m_signalPanel);

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

void SessionWidget::startInterception(const SerialConfig &cfg) {
    // A live session and an offline replay can't share the console; drop the replay.
    if (m_replayer != nullptr && m_replayer->isReplaying()) {
        m_replayer->stop();
        m_replayBtn->setChecked(false);
    }
    m_lastConfig = cfg;
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    m_stats.setSerialConfig(cfg.baud, cfg.dataBits, cfg.parity, cfg.stopBits);
    m_proxy->open(cfg);
}

void SessionWidget::stopInterception() {
    m_proxy->close();
}

QWidget *SessionWidget::buildConsolePanel(QWidget *parent) {
    auto *panel = new QWidget(parent);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 0, 6, 0);
    layout->setSpacing(6);

    m_console = new ConsoleView(panel);

    const auto markToolbarButton = [](QPushButton *button, const char *kind) {
        button->setProperty(kind, true);
        button->setCursor(Qt::PointingHandCursor);
    };
    const auto makeToggle = [&](const QString &text, const QString &tooltip) {
        auto *button = new QPushButton(text, panel);
        button->setCheckable(true);
        button->setToolTip(tooltip);
        markToolbarButton(button, "toolbarToggle");
        return button;
    };
    const auto makeAction = [&](const QString &text, const QString &tooltip) {
        auto *button = new QPushButton(text, panel);
        button->setToolTip(tooltip);
        markToolbarButton(button, "toolbarAction");
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

    // --- Toolbar row 1: format segments + line splitting --------------------
    auto *row1 = new QHBoxLayout();
    row1->setSpacing(6);
    row1->addWidget(makeSectionLabel(QStringLiteral("View")));

    m_hexCheck = makeToggle(QStringLiteral("HEX"), QStringLiteral("Show hexadecimal bytes"));
    m_decCheck = makeToggle(QStringLiteral("DEC"), QStringLiteral("Show decimal byte values"));
    m_binCheck = makeToggle(QStringLiteral("BIN"), QStringLiteral("Show binary byte values"));
    m_asciiCheck = makeToggle(QStringLiteral("ASCII"), QStringLiteral("Show ASCII gutter"));
    m_hexCheck->setChecked(true);
    m_asciiCheck->setChecked(true);
    for (QPushButton *button : {m_hexCheck, m_decCheck, m_binCheck, m_asciiCheck}) {
        button->setProperty("segmentButton", true);
        connect(button, &QPushButton::toggled, this, &SessionWidget::applyFormats);
        row1->addWidget(button);
    }

    row1->addWidget(makeDivider());
    row1->addWidget(makeSectionLabel(QStringLiteral("Split")));
    m_newlineModeBox = new QComboBox(panel);
    m_newlineModeBox->addItem(QStringLiteral("CR"), 0);
    m_newlineModeBox->addItem(QStringLiteral("LF"), 1);
    m_newlineModeBox->addItem(QStringLiteral("CR+LF"), 2);
    m_newlineModeBox->addItem(QStringLiteral("Every packet/chunk"), 3);
    m_newlineModeBox->addItem(QStringLiteral("Every N bytes"), 4);
    m_newlineModeBox->addItem(QStringLiteral("Header byte array"), 5);
    m_newlineModeBox->setCurrentIndex(2);
    m_newlineModeBox->setToolTip(
        QStringLiteral("Split incoming streams into lines by:\n"
                       "CR/LF/CR+LF: carriage return, line feed, or both\n"
                       "Every packet/chunk: one line per captured chunk\n"
                       "Every N bytes: fixed byte count per line\n"
                       "Header byte array: split when header pattern is found"));

    m_newlineParamEdit = new QLineEdit(panel);
    m_newlineParamEdit->setFixedWidth(100);
    m_newlineParamEdit->setPlaceholderText(QStringLiteral("param…"));

    m_newlineFormatBox = new QComboBox(panel);
    m_newlineFormatBox->addItem(QStringLiteral("HEX"));
    m_newlineFormatBox->addItem(QStringLiteral("ASCII"));
    m_newlineFormatBox->addItem(QStringLiteral("DEC"));
    m_newlineFormatBox->addItem(QStringLiteral("BIN"));
    m_newlineFormatBox->setFixedWidth(60);
    m_newlineFormatBox->setToolTip(QStringLiteral("Data format for header pattern input"));
    m_newlineFormatBox->hide();

    connect(m_newlineModeBox, &QComboBox::currentIndexChanged, this, &SessionWidget::applyNewlineMode);
    connect(m_newlineParamEdit, &QLineEdit::editingFinished, this, &SessionWidget::applyNewlineMode);
    connect(m_newlineFormatBox, &QComboBox::currentIndexChanged, this, &SessionWidget::applyNewlineMode);
    row1->addWidget(m_newlineModeBox);
    row1->addWidget(m_newlineFormatBox);
    row1->addWidget(m_newlineParamEdit);

    row1->addStretch(1);
    layout->addLayout(row1);

    // --- Toolbar row 2: flow controls + actions + search --------------------
    auto *row2 = new QHBoxLayout();
    row2->setSpacing(6);
    row2->addWidget(makeSectionLabel(QStringLiteral("Flow")));

    m_autoScrollCheck = makeToggle(QStringLiteral("Auto"), QStringLiteral("Automatically scroll to the end of the log"));
    m_autoScrollCheck->setChecked(true);
    connect(m_autoScrollCheck, &QPushButton::toggled, m_console, &ConsoleView::setAutoScroll);
    m_pauseCheck = makeToggle(QStringLiteral("Pause"), QStringLiteral("Suspend UI viewport updates"));
    connect(m_pauseCheck, &QPushButton::toggled, m_console, &ConsoleView::setPaused);
    m_tsCheck = makeToggle(QStringLiteral("Time"), QStringLiteral("Show or hide the [HH:mm:ss.zzz] timestamp prefix on each line"));
    m_tsCheck->setChecked(true);
    connect(m_tsCheck, &QPushButton::toggled, m_console, &ConsoleView::setShowTimestamps);
    row2->addWidget(m_autoScrollCheck);
    row2->addWidget(m_pauseCheck);
    row2->addWidget(m_tsCheck);

    row2->addWidget(makeDivider());
    m_countsLabel = new QLabel(QStringLiteral("Rx: 0  Tx: 0"), panel);
    m_countsLabel->setObjectName(QStringLiteral("consoleCountsLabel"));
    m_countsLabel->setToolTip(QStringLiteral("Cumulative byte counts. Rate updates every second."));
    row2->addWidget(m_countsLabel);
    auto *resetBtn = makeAction(QStringLiteral("Reset"), QStringLiteral("Clear Tx/Rx byte counters"));
    resetBtn->setToolTip(QStringLiteral("Clear Tx/Rx byte counters"));
    connect(resetBtn, &QPushButton::clicked, m_console, &ConsoleView::resetCounts);
    row2->addWidget(resetBtn);

    row2->addWidget(makeDivider());
    row2->addWidget(makeSectionLabel(QStringLiteral("Actions")));
    auto *clearBtn = makeAction(QStringLiteral("Clear"), QStringLiteral("Clear all text from viewport and raw history cache"));
    connect(clearBtn, &QPushButton::clicked, m_console, &ConsoleView::clearConsole);
    auto *saveBtn = makeAction(QStringLiteral("Save"), QStringLiteral("Export all currently captured plain text to a file"));
    connect(saveBtn, &QPushButton::clicked, this, &SessionWidget::saveReceived);
    m_logBtn = makeAction(QStringLiteral("Log"), QStringLiteral("Continuously append every line to a file"));
    m_logBtn->setCheckable(true);
    connect(m_logBtn, &QPushButton::clicked, this, &SessionWidget::toggleLogging);
    m_captureBtn = makeAction(QStringLiteral("Capture"),
                              QStringLiteral("Record raw traffic to a pcap file (opens in Wireshark) straight from the backend"));
    m_captureBtn->setCheckable(true);
    connect(m_captureBtn, &QPushButton::clicked, this, &SessionWidget::toggleCapture);
    m_replayBtn = makeAction(QStringLiteral("Replay"),
                             QStringLiteral("Open a captured pcap file and replay it through the console with original timing"));
    m_replayBtn->setCheckable(true);
    connect(m_replayBtn, &QPushButton::clicked, this, &SessionWidget::toggleReplay);
    row2->addWidget(clearBtn);
    row2->addWidget(saveBtn);
    row2->addWidget(m_logBtn);
    row2->addWidget(m_captureBtn);
    row2->addWidget(m_replayBtn);

    row2->addWidget(makeDivider());
    m_selLabel = new QLabel(QStringLiteral("Sel: 0"), panel);
    m_selLabel->setObjectName(QStringLiteral("consoleSelectionLabel"));
    row2->addWidget(m_selLabel);

    row2->addStretch(1);
    row2->addWidget(makeSectionLabel(QStringLiteral("Find")));

    // Mode box: how the Find text is interpreted. Order matches ConsoleView::SearchMode.
    auto *searchModeBox = new QComboBox(panel);
    searchModeBox->addItems(
        {QStringLiteral("Auto"), QStringLiteral("Text"), QStringLiteral("HEX"), QStringLiteral("DEC"), QStringLiteral("BIN")});
    searchModeBox->setToolTip(QStringLiteral("Interpret the Find text as: Auto (detect), literal Text, or HEX / DEC / BIN byte values"));
    connect(searchModeBox, &QComboBox::currentIndexChanged, this,
            [this](int index) { m_console->setSearchMode(static_cast<ConsoleView::SearchMode>(index)); });
    row2->addWidget(searchModeBox);

    m_findEdit = new QLineEdit(panel);
    m_findEdit->setPlaceholderText(QStringLiteral("find…"));
    m_findEdit->setFixedWidth(160);
    m_findEdit->setToolTip(
        QStringLiteral("Search the console history. Use the mode box to search by text or HEX / DEC / BIN byte values."));
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { doFind(false); });
    connect(m_findEdit, &QLineEdit::textChanged, m_console, &ConsoleView::highlightSearchText);
    auto *findPrevBtn = makeAction(QStringLiteral("◀"), QStringLiteral("Find previous occurrence"));
    auto *findNextBtn = makeAction(QStringLiteral("▶"), QStringLiteral("Find next occurrence"));
    findPrevBtn->setFixedWidth(32);
    findNextBtn->setFixedWidth(32);
    connect(findPrevBtn, &QPushButton::clicked, this, [this] { doFind(true); });
    connect(findNextBtn, &QPushButton::clicked, this, [this] { doFind(false); });
    row2->addWidget(findPrevBtn);
    row2->addWidget(findNextBtn);
    row2->addWidget(m_findEdit);

    // Live match counter, updated on every highlight pass.
    auto *matchCountLabel = new QLabel(panel);
    matchCountLabel->setToolTip(QStringLiteral("Number of matches for the current search"));
    connect(m_console, &ConsoleView::searchMatchCount, matchCountLabel, [matchCountLabel](int count) {
        matchCountLabel->setText(count > 0 ? QStringLiteral("%1 match%2").arg(count).arg(count == 1 ? QString() : QStringLiteral("es"))
                                           : QString());
    });
    row2->addWidget(matchCountLabel);
    layout->addLayout(row2);

    layout->addWidget(m_console, 1);

    m_injectPanel = new InjectionPanel(panel);
    layout->addWidget(m_injectPanel);

    // --- Macros + send history ---------------------------------------------
    m_macroBar = new MacroBar(panel);
    connect(m_macroBar, &MacroBar::send, this, [this](const QByteArray &bytes, bool toDevice) {
        const bool sent = toDevice ? m_proxy->injectToDevice(bytes) : m_proxy->injectToApp(bytes);
        if (!sent) {
            onError(m_proxy->isRunning() ? QStringLiteral("Macro send failed — send buffer full, bytes dropped.")
                                         : QStringLiteral("Macro send failed — start interception first."));
        }
    });
    layout->addWidget(m_macroBar);

    connect(m_console, &ConsoleView::countsChanged, this, &SessionWidget::updateCounts);
    connect(m_console, &ConsoleView::selectionChars, this,
            [this](int chars) { m_selLabel->setText(QStringLiteral("Sel: %1").arg(chars)); });

    return panel;
}

void SessionWidget::rescanDevices() {
    QDir dev(QStringLiteral("/dev"));
    const QStringList filters{QStringLiteral("ttyUSB*"), QStringLiteral("ttyACM*"), QStringLiteral("ttyS*"), QStringLiteral("ttyAMA*")};
    const auto flags = QDir::System | QDir::AllEntries | QDir::NoDotAndDotDot;
    const QStringList found = dev.entryList(filters, flags, QDir::Name | QDir::LocaleAware);

    QStringList systemPorts;
    for (const QString &name : found) {
        systemPorts.append(QStringLiteral("/dev/") + name);
    }

    QStringList byIdPorts;
    QDir byId(QStringLiteral("/dev/serial/by-id"));
    if (byId.exists()) {
        for (const QString &name : byId.entryList(QDir::System | QDir::NoDotAndDotDot)) {
            byIdPorts.append(byId.filePath(name));
        }
    }

    m_configPanel->populateDevices(systemPorts, byIdPorts);
    m_configPanel->setStatus(QStringLiteral("Device list refreshed."));
}

void SessionWidget::onStarted(const QString &slavePath) {
    m_configPanel->setRunningState(true);
    if (m_lastConfig.directMode) {
        m_configPanel->setStatus(QStringLiteral("Connected directly to: <b>%1</b>").arg(m_configPanel->device()));
    } else {
        m_configPanel->setStatus(QStringLiteral("Intercepting. Point the target app at: <b>%1</b>").arg(slavePath));
    }
    m_injectPanel->setRunningState(true, m_lastConfig.directMode);

    if (m_modemTimer != nullptr) {
        m_modemTimer->start();
        pollModemLines();
    }
    emit sessionTitleChanged(QStringLiteral("[Active] %1").arg(m_configPanel->device()));
}

void SessionWidget::onStopped() {
    m_configPanel->setRunningState(false);
    m_configPanel->setStatus(QStringLiteral("Stopped."));
    m_injectPanel->setRunningState(false, false);
    if (m_modemTimer != nullptr) {
        m_modemTimer->stop();
    }
    emit sessionTitleChanged(QStringLiteral("Session"));
}

void SessionWidget::onError(const QString &message) {
    m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>%1</span>").arg(message));
}

void SessionWidget::applyFormats() {
    m_console->setFormats(m_hexCheck->isChecked(), m_decCheck->isChecked(), m_binCheck->isChecked(), m_asciiCheck->isChecked());
    QSettings settings;
    settings.setValue(QStringLiteral("connection/showHex"), m_hexCheck->isChecked());
    settings.setValue(QStringLiteral("connection/showDec"), m_decCheck->isChecked());
    settings.setValue(QStringLiteral("connection/showBin"), m_binCheck->isChecked());
    settings.setValue(QStringLiteral("connection/showAscii"), m_asciiCheck->isChecked());
}

void SessionWidget::applyNewlineMode() {
    const int idx = m_newlineModeBox->currentIndex();

    // Show/hide format selector only for header byte array mode
    m_newlineFormatBox->setVisible(idx == 5);

    // Show/hide param edit for modes that need it
    const bool needsParam = (idx == 4 || idx == 5);
    m_newlineParamEdit->setVisible(needsParam);

    auto mode = ConsoleView::NewlineMode::PerChunk;
    int param = 0;

    switch (idx) {
        case 0:  // CR split
            mode = ConsoleView::NewlineMode::CrLf;
            param = '\r';
            break;
        case 1:  // LF split
            mode = ConsoleView::NewlineMode::CrLf;
            param = '\n';
            break;
        case 2:  // CR+LF split
            mode = ConsoleView::NewlineMode::CrLf;
            param = 0;  // Both
            break;
        case 3:  // Every packet/chunk
            mode = ConsoleView::NewlineMode::PerChunk;
            break;
        case 4:  // Every N bytes
            mode = ConsoleView::NewlineMode::FixedCount;
            {
                bool ok = false;
                param = m_newlineParamEdit->text().toInt(&ok, 10);
                if (!ok || param <= 0) {
                    param = 16;
                    m_newlineParamEdit->setText(QStringLiteral("16"));
                }
            }
            break;
        case 5:  // Header byte array
            mode = ConsoleView::NewlineMode::Delimiter;
            {
                const QString text = m_newlineParamEdit->text();
                bool ok = false;
                int headerByte = 0;

                // Parse based on selected format
                switch (m_newlineFormatBox->currentIndex()) {
                    case 0:  // HEX
                        headerByte = text.toInt(&ok, 16);
                        break;
                    case 1:  // ASCII
                        if (!text.isEmpty()) {
                            headerByte = static_cast<unsigned char>(text.at(0).toLatin1());
                            ok = true;
                        }
                        break;
                    case 2:  // DEC
                        headerByte = text.toInt(&ok, 10);
                        break;
                    case 3:  // BIN
                        headerByte = text.toInt(&ok, 2);
                        break;
                    default:
                        break;
                }
                param = ok ? (headerByte & 0xFF) : 0xAA;
            }
            break;
        default:
            break;
    }

    m_console->setNewlineMode(mode, param);
}

void SessionWidget::updateCounts(qint64 rx, qint64 tx, qint64 rxRate, qint64 txRate) {
    const auto fmtRate = [](qint64 bps) -> QString {
        if (bps <= 0) {
            return QStringLiteral("0 B/s");
        }
        if (bps >= 1024) {
            return QStringLiteral("%1 KB/s").arg(bps / 1024);
        }
        return QStringLiteral("%1 B/s").arg(bps);
    };
    QString text = QStringLiteral("Rx: %1 (%2)  Tx: %3 (%4)").arg(rx).arg(fmtRate(rxRate)).arg(tx).arg(fmtRate(txRate));
    const quint64 dropped = m_proxy->stats().dropped;
    if (dropped > 0) {
        text += QStringLiteral("  <span style='color:#e57373'>Drop: %1</span>").arg(dropped);
    }
    m_countsLabel->setText(text);
}

void SessionWidget::doFind(bool backward) {
    const QString query = m_findEdit->text();
    if (query.isEmpty()) {
        return;
    }
    QTextDocument::FindFlags flags;
    if (backward) {
        flags |= QTextDocument::FindBackward;
    }
    if (!m_console->findQuery(query, flags)) {
        if (backward) {
            m_console->moveCursorToEnd();
        } else {
            m_console->moveCursorToStart();
        }
        m_console->findQuery(query, flags);
    }
}

void SessionWidget::saveReceived() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save captured data"), QStringLiteral("aetherbus_capture.txt"),
                                                      QStringLiteral("Text files (*.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        onError(QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
        return;
    }
    file.write(m_console->toPlainText().toUtf8());
    file.close();
    m_configPanel->setStatus(QStringLiteral("Saved capture to %1").arg(path));
}

void SessionWidget::toggleLogging() {
    if (m_console->isLogging()) {
        m_console->stopLogging();
        m_logBtn->setChecked(false);
        m_configPanel->setStatus(QStringLiteral("Logging stopped."));
        return;
    }
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Log captured data to file"), QStringLiteral("aetherbus_session.log"),
                                     QStringLiteral("Log files (*.log *.txt);;All files (*)"));
    if (path.isEmpty()) {
        m_logBtn->setChecked(false);
        return;
    }
    if (!m_console->startLogging(path)) {
        m_logBtn->setChecked(false);
        onError(QStringLiteral("Could not open log file: %1").arg(path));
        return;
    }
    m_logBtn->setChecked(true);
    m_configPanel->setStatus(QStringLiteral("Logging to %1").arg(path));
}

void SessionWidget::sendFile() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Send file to device"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        onError(QStringLiteral("Could not read %1: %2").arg(path, file.errorString()));
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    if (!bytes.isEmpty()) {
        if (!m_proxy->injectToDevice(bytes)) {
            onError(m_proxy->isRunning() ? QStringLiteral("Send file failed — send buffer full, bytes dropped.")
                                         : QStringLiteral("Send file failed — start interception first."));
            return;
        }
        m_configPanel->setStatus(QStringLiteral("Sent %1 bytes from %2").arg(bytes.size()).arg(path));
    }
}

void SessionWidget::pollModemLines() {
    const PtyProxy::ModemLines lines = m_proxy->modemLines();
    m_signalPanel->updateModemStatus(lines.cts, lines.dsr, lines.dcd, lines.ri);
}

void SessionWidget::onDisconnected() {
    m_configPanel->setRunningState(false);
    m_injectPanel->setRunningState(false, false);
    if (m_modemTimer != nullptr) {
        m_modemTimer->stop();
    }
    if (m_signalPanel->isAutoReconnectEnabled()) {
        m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Device lost — reconnecting…</span>"));
        m_reconnectTimer->start(1000);
    } else {
        m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Device disconnected.</span>"));
    }
    emit sessionTitleChanged(QStringLiteral("Session"));
}

void SessionWidget::toggleCapture() {
    if (m_proxy->isCapturing()) {
        m_proxy->stopCapture();
        m_captureBtn->setChecked(false);
        m_configPanel->setStatus(QStringLiteral("Capture stopped."));
        return;
    }
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Capture traffic to pcap"), QStringLiteral("aetherbus_capture.pcap"),
                                     QStringLiteral("pcap files (*.pcap);;All files (*)"));
    if (path.isEmpty()) {
        m_captureBtn->setChecked(false);
        return;
    }
    if (!m_proxy->startCapture(path)) {
        m_captureBtn->setChecked(false);
        return;
    }
    m_captureBtn->setChecked(true);
    m_configPanel->setStatus(QStringLiteral("Capturing to %1").arg(path));
}

void SessionWidget::toggleReplay() {
    if (m_replayer->isReplaying()) {
        m_replayer->stop();
        m_replayBtn->setChecked(false);
        m_configPanel->setStatus(QStringLiteral("Replay stopped."));
        return;
    }
    // Replay is offline analysis; refuse while a live session owns the console.
    if (m_proxy->isRunning()) {
        m_replayBtn->setChecked(false);
        onError(QStringLiteral("Stop interception before replaying a capture file."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open capture file for replay"), QString(),
                                                      QStringLiteral("pcap files (*.pcap);;All files (*)"));
    if (path.isEmpty()) {
        m_replayBtn->setChecked(false);
        return;
    }
    QString error;
    if (!m_replayer->load(path, &error)) {
        m_replayBtn->setChecked(false);
        onError(QStringLiteral("Could not open capture: %1").arg(error));
        return;
    }
    m_console->clearConsole();
    m_replayBtn->setChecked(true);
    m_configPanel->setStatus(QStringLiteral("Replaying %1 (%2 packets)…").arg(path).arg(m_replayer->chunkCount()));
    m_replayer->start();
}

void SessionWidget::onReplayFinished() {
    m_replayBtn->setChecked(false);
    m_configPanel->setStatus(QStringLiteral("Replay complete (%1 packets).").arg(m_replayer->chunkCount()));
}

void SessionWidget::onLineReconfigured(int baud, int dataBits, aether::Parity parity, int stopBits) {
    const QString baudText = baud > 0 ? QString::number(baud) : QStringLiteral("custom");
    m_configPanel->setStatus(QStringLiteral("Target reconfigured line → %1 %2%3%4 (mirrored to device)")
                                 .arg(baudText)
                                 .arg(dataBits)
                                 .arg(QChar::fromLatin1(parityCode(parity)))
                                 .arg(stopBits));
    m_stats.setSerialConfig(baud, dataBits, parity, stopBits);
}

void SessionWidget::onWriteStalled(aether::Direction dir, quint64 droppedTotal) {
    const QString side = dir == Direction::Rx ? QStringLiteral("application") : QStringLiteral("device");
    m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>%1 side not draining — dropping data (%2 bytes lost)</span>")
                                 .arg(side)
                                 .arg(droppedTotal));
}

void SessionWidget::onChunkCaptured(const aether::CapturedChunk &chunk) {
    m_stats.addChunk(chunk);
}

}  // namespace aether
