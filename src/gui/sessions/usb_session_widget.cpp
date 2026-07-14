#include "gui/sessions/usb_session_widget.hpp"
#include "core/usb/usb_backend.hpp"
#include "core/usb/usb_parser.hpp"
#include "gui/panels/usb_config_panel.hpp"
#include "gui/panels/usb_injection_panel.hpp"
#include "gui/widgets/console_panel.hpp"
#include "gui/widgets/consoleview.hpp"
#include "gui/widgets/statspanel.hpp"
#include "gui/widgets/collapsible_splitter.hpp"

#include <QTimer>
#include <QVBoxLayout>
#include <QSettings>
#include <QFileDialog>
#include <QJsonObject>
#include <QPushButton>
#include <QSplitter>

namespace aether {

UsbSessionWidget::UsbSessionWidget(QWidget *parent) : SessionView(parent), m_backend(new UsbBackend(this)) {
    buildUi();
    rescan();

    connect(m_configPanel, &UsbConfigPanel::startUsb, this, &UsbSessionWidget::startCapture);
    connect(m_configPanel, &UsbConfigPanel::stopUsb, this, &UsbSessionWidget::stopCapture);
    connect(m_configPanel, &UsbConfigPanel::rescanRequested, this, &UsbSessionWidget::rescan);

    connect(m_backend, &UsbBackend::chunksCaptured, this, &UsbSessionWidget::onChunksCaptured);
    connect(m_backend, &UsbBackend::started, this, &UsbSessionWidget::onStarted);
    connect(m_backend, &UsbBackend::stopped, this, &UsbSessionWidget::onStopped);
    connect(m_backend, &UsbBackend::errorOccurred, this, &UsbSessionWidget::onError);
    connect(m_backend, &UsbBackend::disconnected, this, &UsbSessionWidget::onDisconnected);

    connect(m_consolePanel, &ConsolePanel::logToggled, this, &UsbSessionWidget::toggleLogging);
    connect(m_consolePanel, &ConsolePanel::captureToggled, this, &UsbSessionWidget::toggleCapture);
    m_consolePanel->replayButton()->setVisible(false);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, [this] {
        m_stats.rollRates();
        updateCounts();
    });
    m_statsTimer->start();

    m_consolePanel->console()->setNewlineMode(ConsoleView::NewlineMode::Frame, 0);
    connect(m_consolePanel, &ConsolePanel::formatChanged, this, &UsbSessionWidget::applyFormats);
    applyFormats();
}

UsbSessionWidget::~UsbSessionWidget() {
    if (m_backend) {
        m_backend->disconnect();
        delete m_backend;
        m_backend = nullptr;
    }
}

bool UsbSessionWidget::isRunning() const {
    return m_backend && m_backend->isRunning();
}

void UsbSessionWidget::stopSession() {
    if (m_backend && m_backend->isRunning()) {
        m_backend->close();
    }
}

void UsbSessionWidget::saveSettings(QSettings &settings) const {
    const UsbConfig cfg = m_configPanel->config();
    settings.setValue(QStringLiteral("interfaceName"), cfg.interfaceName);
}

void UsbSessionWidget::loadSettings(const QSettings &settings) {
    const QString name = settings.value(QStringLiteral("interfaceName")).toString();
    rescan();
    m_configPanel->setInterfaceName(name);
}

bool UsbSessionWidget::handleControl(const QString &verb, const QJsonObject &args, QJsonObject &reply, QString *error) {
    const auto fail = [&](const QString &msg) {
        if (error != nullptr) {
            *error = msg;
        }
        return false;
    };

    if (verb == QLatin1String("send")) {
        return fail(QStringLiteral("USB injection (transmit) is not implemented."));
    }

    if (verb == QLatin1String("stop")) {
        stopSession();
        return true;
    }

    if (verb == QLatin1String("stats")) {
        reply.insert(QStringLiteral("stats"), statsToControlJson(m_stats, isRunning()));
        return true;
    }

    if (verb == QLatin1String("capture")) {
        const QString action = args.value(QStringLiteral("action")).toString(QStringLiteral("status"));
        if (action == QLatin1String("start")) {
            const QString path = args.value(QStringLiteral("path")).toString();
            if (path.isEmpty()) {
                return fail(QStringLiteral("capture start: 'path' is required"));
            }
            QString openErr;
            if (!m_captureWriter.open(path, m_backend->linkType(), &openErr)) {
                return fail(QStringLiteral("capture start failed — %1").arg(openErr));
            }
            m_consolePanel->captureButton()->setChecked(true);
        } else if (action == QLatin1String("stop")) {
            m_captureWriter.close();
            m_consolePanel->captureButton()->setChecked(false);
        } else if (action != QLatin1String("status")) {
            return fail(QStringLiteral("capture: 'action' must be 'start', 'stop' or 'status'"));
        }
        reply.insert(QStringLiteral("capturing"), m_captureWriter.isOpen());
        return true;
    }

    return fail(QStringLiteral("USB session does not support verb '%1'").arg(verb));
}

bool UsbSessionWidget::startSession(QString *error) {
    if (error != nullptr) {
        *error = QStringLiteral("USB start over the control channel is not supported");
    }
    return false;
}

bool UsbSessionWidget::applyControlConfig(const QJsonObject &config, QString *error) {
    Q_UNUSED(config);
    if (error != nullptr) {
        *error = QStringLiteral("USB config over the control channel is not supported");
    }
    return false;
}

void UsbSessionWidget::startCapture(const UsbConfig &cfg) {
    if (cfg.interfaceName.isEmpty()) {
        onError(tr("Interface name cannot be empty."));
        return;
    }
    m_consolePanel->console()->clearConsole();
    m_stats.reset();
    m_backend->open(cfg);
}

void UsbSessionWidget::stopCapture() {
    m_backend->close();
    if (m_captureWriter.isOpen()) {
        m_captureWriter.close();
        m_consolePanel->captureButton()->setChecked(false);
    }
}

void UsbSessionWidget::rescan() {
    m_configPanel->setInterfaces(UsbBackend::listInterfaces());
}

void UsbSessionWidget::onStarted(const QString &info) {
    m_configPanel->setRunning(true);
    m_consolePanel->logButton()->setChecked(true);
    m_configPanel->setStatus(tr("Capturing: %1").arg(info));
    emit sessionTitleChanged(QStringLiteral("[USB] %1").arg(m_configPanel->config().interfaceName));
}

void UsbSessionWidget::onStopped() {
    m_configPanel->setRunning(false);
    m_consolePanel->logButton()->setChecked(false);
    m_configPanel->setStatus(QStringLiteral("Stopped."));
    emit sessionTitleChanged(QStringLiteral("USB Session"));
}

void UsbSessionWidget::onError(const QString &message) {
    m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>%1</span>").arg(message));
}

void UsbSessionWidget::onDisconnected() {
    m_configPanel->setRunning(false);
    m_consolePanel->logButton()->setChecked(false);
    m_configPanel->setStatus(QStringLiteral("<span style='color:#e57373'>Interface lost.</span>"));
    emit sessionTitleChanged(QStringLiteral("USB Session"));
}

void UsbSessionWidget::onChunksCaptured(const QVector<CapturedChunk> &chunks) {
    ConsoleView *console = m_consolePanel->console();
    for (const CapturedChunk &chunk : chunks) {
        m_stats.addChunk(chunk);

        if (m_captureWriter.isOpen()) {
            m_captureWriter.writePacket(chunk.timestampMs, chunk.data);
        }

        UsbUrbInfo urb = UsbParser::parseUrb(chunk.data);
        if (urb.isValid) {
            CapturedChunk frameChunk;
            frameChunk.timestampMs = chunk.timestampMs;
            frameChunk.dir = chunk.dir;
            frameChunk.isFrame = true;
            frameChunk.extraInfo = urb.infoText;
            frameChunk.data = urb.payload;
            console->appendChunk(frameChunk);
        }
    }
    emit controlTraffic(chunks);
}

void UsbSessionWidget::applyFormats() {
    m_consolePanel->console()->setFormats(m_consolePanel->isHexChecked(), m_consolePanel->isDecChecked(), m_consolePanel->isBinChecked(),
                                          m_consolePanel->isAsciiChecked());
}

void UsbSessionWidget::updateCounts() {
    // Mirror the RX/TX stats from the calculator.
}

void UsbSessionWidget::toggleLogging() {
    if (isRunning()) {
        stopCapture();
    } else {
        startCapture(m_configPanel->config());
    }
}

void UsbSessionWidget::toggleCapture() {
    if (m_captureWriter.isOpen()) {
        m_captureWriter.close();
        m_consolePanel->captureButton()->setChecked(false);
        m_configPanel->setStatus(tr("Capture file closed."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Capture USB traffic to pcap"), QStringLiteral("aetherbus_usb_capture.pcap"),
                                                      tr("pcap files (*.pcap);;All files (*)"));
    if (path.isEmpty()) {
        m_consolePanel->captureButton()->setChecked(false);
        return;
    }

    QString error;
    uint32_t dlt = m_backend->linkType();
    if (!m_captureWriter.open(path, dlt, &error)) {
        m_consolePanel->captureButton()->setChecked(false);
        onError(tr("Could not open capture file: %1").arg(error));
        return;
    }

    m_consolePanel->captureButton()->setChecked(true);
    m_configPanel->setStatus(tr("Capturing to %1").arg(path));
}

void UsbSessionWidget::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    m_mainSplitter = new CollapsibleSplitter(Qt::Horizontal, this);
    m_mainSplitter->setObjectName(QStringLiteral("mainSplitter"));

    auto *leftColumn = new QWidget(m_mainSplitter);
    leftColumn->setMinimumWidth(320);
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    m_configPanel = new UsbConfigPanel(leftColumn);
    leftLayout->addWidget(m_configPanel);

    m_statsPanel = new StatsPanel(leftColumn);
    m_statsPanel->setActiveCalculator(&m_stats);
    m_statsPanel->setMinimumHeight(360);
    leftLayout->addWidget(m_statsPanel, 1);

    m_mainSplitter->addWidget(leftColumn);
    m_mainSplitter->addWidget(buildConsolePanel(this));

    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({340, 900});
    m_mainSplitter->setPaneCollapsible(0);

    outer->addWidget(m_mainSplitter);
}

QWidget *UsbSessionWidget::buildConsolePanel(QWidget *parent) {
    auto *panel = new QWidget(parent);
    panel->setMinimumWidth(420);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *splitter = new QSplitter(Qt::Vertical, panel);

    m_consolePanel = new ConsolePanel(splitter);
    m_consolePanel->setSplitControlsVisible(false);
    m_consolePanel->setExtraActionsVisible(false);
    m_consolePanel->setSelectionLabelVisible(false);
    splitter->addWidget(m_consolePanel);

    m_injectionPanel = new UsbInjectionPanel(splitter);
    splitter->addWidget(m_injectionPanel);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({600, 300});

    layout->addWidget(splitter, 1);

    connect(m_consolePanel, &ConsolePanel::saveRequested, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save USB log"), QStringLiteral("aetherbus_usb.txt"),
                                                          QStringLiteral("Text files (*.txt);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QFile::WriteOnly | QFile::Text)) {
            file.write(m_consolePanel->console()->toPlainText().toUtf8());
        }
    });

    return panel;
}

}  // namespace aether
