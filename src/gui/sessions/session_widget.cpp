#include "gui/sessions/session_widget.hpp"

#include "core/common/capture_replay.hpp"
#include "core/common/format_codec.hpp"

#include <QJsonObject>
#include "core/serial/pty_proxy.hpp"
#include "gui/widgets/consoleview.hpp"
#include "gui/widgets/console_panel.hpp"
#include "gui/widgets/macrobar.hpp"
#include "gui/widgets/statspanel.hpp"
#include "gui/widgets/collapsible_splitter.hpp"
#include "gui/panels/config_panel.hpp"
#include "gui/panels/signal_panel.hpp"
#include "gui/panels/injection_panel.hpp"

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

SessionWidget::SessionWidget(QWidget *parent) : SessionView(parent), m_proxy(new PtyProxy(this)) {
    buildUi();
    rescanDevices();

    // Wire Up Panels
    connect(m_configPanel, &ConfigPanel::startInterception, this, &SessionWidget::startInterception);
    connect(m_configPanel, &ConfigPanel::stopInterception, this, &SessionWidget::stopInterception);
    connect(m_configPanel, &ConfigPanel::rescanRequested, this, &SessionWidget::rescanDevices);
    connect(m_configPanel, &ConfigPanel::statusChanged, this, &SessionWidget::statusMessage);

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
    connect(m_injectPanel, &InjectionPanel::saveAsMacroRequested, this,
            [this](int format, const QString &payload, int ending, bool toDevice) {
                if (m_macroBar) {
                    m_macroBar->addMacroFromState(format, payload, ending, toDevice);
                }
            });

    // Proxy connections. One queued event per worker wakeup (batched), not
    // one per receiver per chunk.
    connect(m_proxy, &PtyProxy::chunksCaptured, this, &SessionWidget::onChunksCaptured);
    connect(m_proxy, &PtyProxy::started, this, &SessionWidget::onStarted);
    connect(m_proxy, &PtyProxy::stopped, this, &SessionWidget::onStopped);
    connect(m_proxy, &PtyProxy::errorOccurred, this, &SessionWidget::onError);
    connect(m_proxy, &PtyProxy::disconnected, this, &SessionWidget::onDisconnected);
    connect(m_proxy, &PtyProxy::lineReconfigured, this, &SessionWidget::onLineReconfigured);
    connect(m_proxy, &PtyProxy::writeStalled, this, &SessionWidget::onWriteStalled);

    // Offline replay feeds chunks through the same rendering path as live capture.
    m_replayer = new CaptureReplayer(this);
    connect(m_replayer, &CaptureReplayer::chunkReplayed, m_consolePanel->console(), &ConsoleView::appendChunk);
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

    // Console Panel configuration connections
    connect(m_consolePanel, &ConsolePanel::formatChanged, this, &SessionWidget::applyFormats);
    connect(m_consolePanel, &ConsolePanel::newlineModeChanged, this, &SessionWidget::applyNewlineMode);
    connect(m_consolePanel, &ConsolePanel::saveRequested, this, &SessionWidget::saveReceived);
    connect(m_consolePanel, &ConsolePanel::logToggled, this, &SessionWidget::toggleLogging);
    connect(m_consolePanel, &ConsolePanel::captureToggled, this, &SessionWidget::toggleCapture);
    connect(m_consolePanel, &ConsolePanel::replayToggled, this, &SessionWidget::toggleReplay);

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

void SessionWidget::saveSettings(QSettings &settings) const {
    m_configPanel->saveSettings(settings);
    settings.setValue(QStringLiteral("layout/mainSplitterState"), m_mainSplitter->saveState());
}

void SessionWidget::loadSettings(const QSettings &settings) {
    m_configPanel->loadSettings(settings);
    m_mainSplitter->restoreState(settings.value(QStringLiteral("layout/mainSplitterState")).toByteArray());
}

void SessionWidget::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    m_mainSplitter = new CollapsibleSplitter(Qt::Horizontal, this);
    m_mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    auto *mainSplitter = m_mainSplitter;

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
    m_mainSplitter->setPaneCollapsible(0);

    outer->addWidget(mainSplitter);
}

void SessionWidget::startInterception(const SerialConfig &cfg) {
    // A live session and an offline replay can't share the console; drop the replay.
    if (m_replayer != nullptr && m_replayer->isReplaying()) {
        m_replayer->stop();
        m_consolePanel->replayButton()->setChecked(false);
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
    // Keeps the toolbar's button row from wrapping/clipping when this session
    // is squeezed into a tile in a multi-session tiled grid.
    panel->setMinimumWidth(420);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_consolePanel = new ConsolePanel(panel);
    layout->addWidget(m_consolePanel, 1);

    m_injectPanel = new InjectionPanel(panel);
    layout->addWidget(m_injectPanel);

    m_macroBar = new MacroBar(panel);
    connect(m_macroBar, &MacroBar::send, this, [this](const QByteArray &bytes, bool toDevice) {
        const bool sent = toDevice ? m_proxy->injectToDevice(bytes) : m_proxy->injectToApp(bytes);
        if (!sent) {
            onError(m_proxy->isRunning() ? QStringLiteral("Macro send failed — send buffer full, bytes dropped.")
                                         : QStringLiteral("Macro send failed — start interception first."));
        }
    });
    layout->addWidget(m_macroBar);

    connect(m_consolePanel->console(), &ConsoleView::countsChanged, this, &SessionWidget::updateCounts);

    return panel;
}

void SessionWidget::rescanDevices() {
    QStringList systemPorts;
    QStringList byIdPorts;

#if defined(Q_OS_WIN)
    QSettings comSettings(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM"), QSettings::NativeFormat);
    for (const QString &key : comSettings.allKeys()) {
        systemPorts.append(comSettings.value(key).toString());
    }
#elif defined(Q_OS_MAC)
    QDir dev(QStringLiteral("/dev"));
    const QStringList filters{QStringLiteral("cu.*"), QStringLiteral("tty.*")};
    const auto flags = QDir::System | QDir::AllEntries | QDir::NoDotAndDotDot;
    const QStringList found = dev.entryList(filters, flags, QDir::Name | QDir::LocaleAware);
    for (const QString &name : found) {
        systemPorts.append(QStringLiteral("/dev/") + name);
    }
#else
    QDir dev(QStringLiteral("/dev"));
    const QStringList filters{QStringLiteral("ttyUSB*"), QStringLiteral("ttyACM*"), QStringLiteral("ttyS*"), QStringLiteral("ttyAMA*")};
    const auto flags = QDir::System | QDir::AllEntries | QDir::NoDotAndDotDot;
    const QStringList found = dev.entryList(filters, flags, QDir::Name | QDir::LocaleAware);
    for (const QString &name : found) {
        systemPorts.append(QStringLiteral("/dev/") + name);
    }

    QDir byId(QStringLiteral("/dev/serial/by-id"));
    if (byId.exists()) {
        for (const QString &name : byId.entryList(QDir::System | QDir::NoDotAndDotDot)) {
            byIdPorts.append(byId.filePath(name));
        }
    }
#endif

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
    m_consolePanel->console()->setFormats(m_consolePanel->isHexChecked(), m_consolePanel->isDecChecked(), m_consolePanel->isBinChecked(),
                                          m_consolePanel->isAsciiChecked());
    QSettings settings;
    settings.setValue(QStringLiteral("connection/showHex"), m_consolePanel->isHexChecked());
    settings.setValue(QStringLiteral("connection/showDec"), m_consolePanel->isDecChecked());
    settings.setValue(QStringLiteral("connection/showBin"), m_consolePanel->isBinChecked());
    settings.setValue(QStringLiteral("connection/showAscii"), m_consolePanel->isAsciiChecked());
}

void SessionWidget::applyNewlineMode() {
    const int idx = m_consolePanel->newlineModeBox()->currentIndex();

    // Show/hide format selector only for header byte array mode
    m_consolePanel->newlineFormatBox()->setVisible(idx == 5);

    // Show/hide param edit for modes that need it
    const bool needsParam = (idx == 4 || idx == 5);
    m_consolePanel->newlineParamEdit()->setVisible(needsParam);

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
                param = m_consolePanel->newlineParamEdit()->text().toInt(&ok, 10);
                if (!ok || param <= 0) {
                    param = 16;
                    m_consolePanel->newlineParamEdit()->setText(QStringLiteral("16"));
                }
            }
            break;
        case 5:  // Header byte array
            mode = ConsoleView::NewlineMode::Delimiter;
            {
                const QString text = m_consolePanel->newlineParamEdit()->text();
                bool ok = false;
                int headerByte = 0;

                // Parse based on selected format
                switch (m_consolePanel->newlineFormatBox()->currentIndex()) {
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

    m_consolePanel->console()->setNewlineMode(mode, param);
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
    m_consolePanel->setCountsText(text);
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
    file.write(m_consolePanel->console()->toPlainText().toUtf8());
    file.close();
    m_configPanel->setStatus(QStringLiteral("Saved capture to %1").arg(path));
}

void SessionWidget::toggleLogging() {
    if (m_consolePanel->console()->isLogging()) {
        m_consolePanel->console()->stopLogging();
        m_consolePanel->logButton()->setChecked(false);
        m_configPanel->setStatus(QStringLiteral("Logging stopped."));
        return;
    }
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Log captured data to file"), QStringLiteral("aetherbus_session.log"),
                                     QStringLiteral("Log files (*.log *.txt);;All files (*)"));
    if (path.isEmpty()) {
        m_consolePanel->logButton()->setChecked(false);
        return;
    }
    if (!m_consolePanel->console()->startLogging(path)) {
        m_consolePanel->logButton()->setChecked(false);
        onError(QStringLiteral("Could not open log file: %1").arg(path));
        return;
    }
    m_consolePanel->logButton()->setChecked(true);
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
        m_consolePanel->captureButton()->setChecked(false);
        m_configPanel->setStatus(QStringLiteral("Capture stopped."));
        return;
    }
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Capture traffic to pcap"), QStringLiteral("aetherbus_capture.pcap"),
                                     QStringLiteral("pcap files (*.pcap);;All files (*)"));
    if (path.isEmpty()) {
        m_consolePanel->captureButton()->setChecked(false);
        return;
    }
    if (!m_proxy->startCapture(path)) {
        m_consolePanel->captureButton()->setChecked(false);
        return;
    }
    m_consolePanel->captureButton()->setChecked(true);
    m_configPanel->setStatus(QStringLiteral("Capturing to %1").arg(path));
}

void SessionWidget::toggleReplay() {
    if (m_replayer->isReplaying()) {
        m_replayer->stop();
        m_consolePanel->replayButton()->setChecked(false);
        m_configPanel->setStatus(QStringLiteral("Replay stopped."));
        return;
    }
    // Replay is offline analysis; refuse while a live session owns the console.
    if (m_proxy->isRunning()) {
        m_consolePanel->replayButton()->setChecked(false);
        onError(QStringLiteral("Stop interception before replaying a capture file."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open capture file for replay"), QString(),
                                                      QStringLiteral("pcap files (*.pcap);;All files (*)"));
    if (path.isEmpty()) {
        m_consolePanel->replayButton()->setChecked(false);
        return;
    }
    QString error;
    if (!m_replayer->load(path, &error)) {
        m_consolePanel->replayButton()->setChecked(false);
        onError(QStringLiteral("Could not open capture: %1").arg(error));
        return;
    }
    m_consolePanel->console()->clearConsole();
    m_consolePanel->replayButton()->setChecked(true);
    m_configPanel->setStatus(QStringLiteral("Replaying %1 (%2 packets)…").arg(path).arg(m_replayer->chunkCount()));
    m_replayer->start();
}

void SessionWidget::onReplayFinished() {
    m_consolePanel->replayButton()->setChecked(false);
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

void SessionWidget::onChunksCaptured(const QVector<aether::CapturedChunk> &chunks) {
    ConsoleView *console = m_consolePanel->console();
    for (const CapturedChunk &chunk : chunks) {
        console->appendChunk(chunk);
        m_stats.addChunk(chunk);
    }
    emit controlTraffic(chunks);
}

bool SessionWidget::sendControl(const QJsonObject &cmd, QString *error) {
    const auto fail = [&](const QString &msg) {
        if (error != nullptr) {
            *error = msg;
        }
        return false;
    };

    QByteArray bytes;
    if (!codec::parseCompactHex(cmd.value(QStringLiteral("data")).toString(), bytes)) {
        return fail(QStringLiteral("serial send: 'data' must be a hex string"));
    }
    if (bytes.isEmpty()) {
        return fail(QStringLiteral("serial send: 'data' is empty"));
    }

    // side=device (default) injects toward the physical UART; side=app injects
    // toward the slave-PTY / target application.
    const QString side = cmd.value(QStringLiteral("side")).toString(QStringLiteral("device"));
    bool ok = false;
    if (side == QLatin1String("app")) {
        ok = m_proxy->injectToApp(bytes);
    } else if (side == QLatin1String("device")) {
        ok = m_proxy->injectToDevice(bytes);
    } else {
        return fail(QStringLiteral("serial send: 'side' must be 'device' or 'app'"));
    }
    if (!ok) {
        return fail(m_proxy->isRunning() ? QStringLiteral("serial send failed — write buffer full")
                                         : QStringLiteral("serial send failed — interception not started"));
    }
    return true;
}

}  // namespace aether
