#include "gui/mainwindow.h"

#include "core/format_codec.h"
#include "core/pty_proxy.h"
#include "core/serial_types.h"
#include "gui/consoleview.h"
#include "gui/theme_controller.h"
#include "aether/version.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QMessageBox>
#include <QSettings>
#include <QApplication>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace aether {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_proxy(new PtyProxy(this)), m_console(nullptr) {
    m_theme = new ThemeController(qApp, this);
    QSettings settings;
    m_theme->setMode(ThemeController::modeFromString(settings.value(QStringLiteral("ui/theme")).toString()));

    buildUi();
    populateDevices();

    // Setup Menu Bar
    QMenuBar *menu = menuBar();

    QMenu *viewMenu = menu->addMenu(tr("&View"));
    QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));

    const auto addThemeAction = [&](const QString &text, ThemeController::Mode mode) {
        QAction *act = themeMenu->addAction(text);
        act->setCheckable(true);
        act->setData(static_cast<int>(mode));
        return act;
    };

    QAction *sys = addThemeAction(tr("&System"), ThemeController::Mode::System);
    QAction *light = addThemeAction(tr("&Light"), ThemeController::Mode::Light);
    QAction *dark = addThemeAction(tr("&Dark"), ThemeController::Mode::Dark);

    auto *themeGroup = new QActionGroup(this);
    themeGroup->addAction(sys);
    themeGroup->addAction(light);
    themeGroup->addAction(dark);
    themeGroup->setExclusive(true);

    switch (m_theme->mode()) {
        case ThemeController::Mode::Light:
            light->setChecked(true);
            break;
        case ThemeController::Mode::Dark:
            dark->setChecked(true);
            break;
        case ThemeController::Mode::System:
            sys->setChecked(true);
            break;
    }

    connect(themeGroup, &QActionGroup::triggered, this, [this](QAction *act) {
        const auto mode = static_cast<ThemeController::Mode>(act->data().toInt());
        m_theme->setMode(mode);
        QSettings settings;
        settings.setValue(QStringLiteral("ui/theme"), ThemeController::modeToString(mode));
    });

    QMenu *helpMenu = menu->addMenu(tr("&Help"));
    QAction *aboutAct = helpMenu->addAction(tr("&About AetherBus…"));
    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About AetherBus"),
                           tr("<h3>AetherBus %1</h3>"
                              "<p>A modern, lightweight, high-performance cross-platform message bus GUI & CLI dashboard.</p>"
                              "<p>Built with <b>Qt %2</b> and C++17/C++20.</p>"
                              "<p>License: <b>MIT</b> &nbsp;·&nbsp; Copyright &copy; 2026 AetherBus Project</p>")
                               .arg(QString::fromLatin1(AETHER_VERSION_STRING), QString::fromLatin1(qVersion())));
    });

    // Load config persistence
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

    connect(m_proxy, &PtyProxy::chunkCaptured, m_console, &ConsoleView::appendChunk);
    connect(m_proxy, &PtyProxy::started, this, &MainWindow::onStarted);
    connect(m_proxy, &PtyProxy::stopped, this, &MainWindow::onStopped);
    connect(m_proxy, &PtyProxy::errorOccurred, this, &MainWindow::onError);
    connect(m_proxy, &PtyProxy::disconnected, this, &MainWindow::onDisconnected);

    setWindowTitle(QStringLiteral("AetherBus — Serial Interceptor"));
    resize(1000, 720);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    auto *central = new QWidget(this);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(4, 4, 4, 4);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, central);

    auto *leftSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    leftSplitter->addWidget(buildConfigPanel(central));
    leftSplitter->addWidget(buildSignalPanel(central));
    leftSplitter->setStretchFactor(0, 0);
    leftSplitter->setStretchFactor(1, 1);

    mainSplitter->addWidget(leftSplitter);
    mainSplitter->addWidget(buildConsolePanel(central));
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);

    // Initial width distribution: sidebar gets 300px, console gets 700px
    mainSplitter->setSizes({300, 700});

    outer->addWidget(mainSplitter);
    setCentralWidget(central);
}

QWidget *MainWindow::buildConfigPanel(QWidget *parent) {
    auto *configGroup = new QGroupBox(QStringLiteral("Interception"), parent);
    auto *form = new QFormLayout(configGroup);

    m_deviceBox = new QComboBox(configGroup);
    m_deviceBox->setEditable(true);
    form->addRow(QStringLiteral("Physical device"), m_deviceBox);

    m_baudBox = new QComboBox(configGroup);
    m_baudBox->setEditable(true);  // allow arbitrary/custom rates
    for (const int baud : {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600}) {
        m_baudBox->addItem(QString::number(baud), baud);
    }
    m_baudBox->setCurrentText(QStringLiteral("115200"));
    form->addRow(QStringLiteral("Baud rate"), m_baudBox);

    m_dataBitsBox = new QComboBox(configGroup);
    for (const int bits : {5, 6, 7, 8}) {
        m_dataBitsBox->addItem(QString::number(bits), bits);
    }
    m_dataBitsBox->setCurrentText(QStringLiteral("8"));
    form->addRow(QStringLiteral("Data bits"), m_dataBitsBox);

    m_parityBox = new QComboBox(configGroup);
    m_parityBox->addItem(QStringLiteral("None"), QChar('N'));
    m_parityBox->addItem(QStringLiteral("Even"), QChar('E'));
    m_parityBox->addItem(QStringLiteral("Odd"), QChar('O'));
    form->addRow(QStringLiteral("Parity"), m_parityBox);

    m_stopBitsBox = new QComboBox(configGroup);
    m_stopBitsBox->addItem(QStringLiteral("1"), 1);
    m_stopBitsBox->addItem(QStringLiteral("2"), 2);
    form->addRow(QStringLiteral("Stop bits"), m_stopBitsBox);

    m_flowBox = new QComboBox(configGroup);
    m_flowBox->addItem(QStringLiteral("None"), static_cast<int>(FlowControl::None));
    m_flowBox->addItem(QStringLiteral("RTS/CTS"), static_cast<int>(FlowControl::RtsCts));
    m_flowBox->addItem(QStringLiteral("XON/XOFF"), static_cast<int>(FlowControl::XonXoff));
    form->addRow(QStringLiteral("Flow control"), m_flowBox);

    m_symlinkEdit = new QLineEdit(configGroup);
    m_symlinkEdit->setPlaceholderText(QStringLiteral("optional, e.g. ./ttyUSB0_sniffed"));
    form->addRow(QStringLiteral("Slave symlink"), m_symlinkEdit);

    m_directCheck = new QCheckBox(QStringLiteral("Direct Connection (Bypass Proxy)"), configGroup);
    form->addRow(m_directCheck);

    m_startButton = new QPushButton(QStringLiteral("Start Interception"), configGroup);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::toggleProxy);
    form->addRow(m_startButton);

    // Toggle fields based on direct check
    connect(m_directCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_symlinkEdit->setEnabled(!checked);
        if (checked) {
            m_symlinkEdit->clear();
            m_startButton->setText(QStringLiteral("Connect Direct"));
        } else {
            m_startButton->setText(QStringLiteral("Start Interception"));
        }
    });

    m_statusLabel = new QLabel(QStringLiteral("Idle."), configGroup);
    m_statusLabel->setWordWrap(true);
    form->addRow(QStringLiteral("Status"), m_statusLabel);

    return configGroup;
}

QWidget *MainWindow::buildConsolePanel(QWidget *parent) {
    auto *panel = new QWidget(parent);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    m_console = new ConsoleView(panel);

    // --- Toolbar row 1: formats + newline rule + control chars --------------
    auto *row1 = new QHBoxLayout();
    m_hexCheck = new QCheckBox(QStringLiteral("HEX"), panel);
    m_decCheck = new QCheckBox(QStringLiteral("DEC"), panel);
    m_binCheck = new QCheckBox(QStringLiteral("BIN"), panel);
    m_asciiCheck = new QCheckBox(QStringLiteral("ASCII"), panel);
    m_hexCheck->setChecked(true);
    m_asciiCheck->setChecked(true);
    for (QCheckBox *c : {m_hexCheck, m_decCheck, m_binCheck, m_asciiCheck}) {
        connect(c, &QCheckBox::toggled, this, &MainWindow::applyFormats);
        row1->addWidget(c);
    }

    row1->addSpacing(12);
    row1->addWidget(new QLabel(QStringLiteral("Newline:"), panel));
    m_newlineModeBox = new QComboBox(panel);
    m_newlineModeBox->addItem(QStringLiteral("Per chunk"));
    m_newlineModeBox->addItem(QStringLiteral("On delimiter (hex)"));
    m_newlineModeBox->addItem(QStringLiteral("Every N bytes"));
    m_newlineModeBox->setCurrentIndex(1);
    m_newlineParamEdit = new QLineEdit(QStringLiteral("0A"), panel);
    m_newlineParamEdit->setFixedWidth(64);
    connect(m_newlineModeBox, &QComboBox::currentIndexChanged, this, &MainWindow::applyNewlineMode);
    connect(m_newlineParamEdit, &QLineEdit::editingFinished, this, &MainWindow::applyNewlineMode);
    row1->addWidget(m_newlineModeBox);
    row1->addWidget(m_newlineParamEdit);

    m_showCtrlCheck = new QCheckBox(QStringLiteral("Show ctrl"), panel);
    connect(m_showCtrlCheck, &QCheckBox::toggled, m_console, &ConsoleView::setShowControlChars);
    row1->addWidget(m_showCtrlCheck);
    row1->addStretch(1);
    layout->addLayout(row1);

    // --- Toolbar row 2: scroll/pause + counters + actions + find ------------
    auto *row2 = new QHBoxLayout();
    m_autoScrollCheck = new QCheckBox(QStringLiteral("Autoscroll"), panel);
    m_autoScrollCheck->setChecked(true);
    connect(m_autoScrollCheck, &QCheckBox::toggled, m_console, &ConsoleView::setAutoScroll);
    m_pauseCheck = new QCheckBox(QStringLiteral("Pause"), panel);
    connect(m_pauseCheck, &QCheckBox::toggled, m_console, &ConsoleView::setPaused);
    row2->addWidget(m_autoScrollCheck);
    row2->addWidget(m_pauseCheck);

    row2->addSpacing(12);
    m_countsLabel = new QLabel(QStringLiteral("Rx: 0   Tx: 0"), panel);
    row2->addWidget(m_countsLabel);
    auto *resetBtn = new QPushButton(QStringLiteral("Reset"), panel);
    connect(resetBtn, &QPushButton::clicked, m_console, &ConsoleView::resetCounts);
    row2->addWidget(resetBtn);

    row2->addSpacing(12);
    auto *clearBtn = new QPushButton(QStringLiteral("Clear"), panel);
    connect(clearBtn, &QPushButton::clicked, m_console, &ConsoleView::clearConsole);
    auto *saveBtn = new QPushButton(QStringLiteral("Save…"), panel);
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::saveReceived);
    m_logBtn = new QPushButton(QStringLiteral("Log…"), panel);
    m_logBtn->setCheckable(true);
    m_logBtn->setToolTip(QStringLiteral("Continuously append every line to a file"));
    connect(m_logBtn, &QPushButton::clicked, this, &MainWindow::toggleLogging);
    row2->addWidget(clearBtn);
    row2->addWidget(saveBtn);
    row2->addWidget(m_logBtn);

    row2->addSpacing(12);
    m_selLabel = new QLabel(QStringLiteral("Sel: 0"), panel);
    row2->addWidget(m_selLabel);

    row2->addStretch(1);
    row2->addWidget(new QLabel(QStringLiteral("Find:"), panel));
    m_findEdit = new QLineEdit(panel);
    m_findEdit->setPlaceholderText(QStringLiteral("text…"));
    m_findEdit->setFixedWidth(160);
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { doFind(false); });
    connect(m_findEdit, &QLineEdit::textChanged, m_console, &ConsoleView::highlightSearchText);
    auto *findPrevBtn = new QPushButton(QStringLiteral("◀"), panel);
    auto *findNextBtn = new QPushButton(QStringLiteral("▶"), panel);
    findPrevBtn->setFixedWidth(32);
    findNextBtn->setFixedWidth(32);
    connect(findPrevBtn, &QPushButton::clicked, this, [this] { doFind(true); });
    connect(findNextBtn, &QPushButton::clicked, this, [this] { doFind(false); });
    row2->addWidget(m_findEdit);
    row2->addWidget(findPrevBtn);
    row2->addWidget(findNextBtn);
    layout->addLayout(row2);

    layout->addWidget(m_console, 1);

    // --- Injection panel ----------------------------------------------------
    auto *injectRow = new QHBoxLayout();
    m_injectFormatBox = new QComboBox(panel);
    m_injectFormatBox->addItem(QStringLiteral("HEX"));
    m_injectFormatBox->addItem(QStringLiteral("ASCII"));
    m_injectFormatBox->addItem(QStringLiteral("DEC"));
    m_injectFormatBox->addItem(QStringLiteral("BIN"));
    m_injectEdit = new QLineEdit(panel);
    m_injectEdit->setPlaceholderText(QStringLiteral("bytes to inject (e.g. 41 42 0D 0A, or text)"));
    m_injectEndingBox = new QComboBox(panel);
    m_injectEndingBox->addItem(QStringLiteral("No ending"));
    m_injectEndingBox->addItem(QStringLiteral("CR"));
    m_injectEndingBox->addItem(QStringLiteral("LF"));
    m_injectEndingBox->addItem(QStringLiteral("CR+LF"));
    auto *toDeviceBtn = new QPushButton(QStringLiteral("Send → Device"), panel);
    m_toAppBtn = new QPushButton(QStringLiteral("Send → App"), panel);
    auto *fileBtn = new QPushButton(QStringLiteral("File…"), panel);
    fileBtn->setToolTip(QStringLiteral("Send the raw contents of a file to the device"));
    connect(toDeviceBtn, &QPushButton::clicked, this, [this] { sendInjection(true); });
    connect(m_toAppBtn, &QPushButton::clicked, this, [this] { sendInjection(false); });
    connect(fileBtn, &QPushButton::clicked, this, &MainWindow::sendFile);
    connect(m_injectEdit, &QLineEdit::returnPressed, this, [this] { sendInjection(true); });
    injectRow->addWidget(m_injectFormatBox);
    injectRow->addWidget(m_injectEdit, 1);
    injectRow->addWidget(m_injectEndingBox);
    injectRow->addWidget(toDeviceBtn);
    injectRow->addWidget(m_toAppBtn);
    injectRow->addWidget(fileBtn);
    layout->addLayout(injectRow);

    // --- Repeat / periodic send row ----------------------------------------
    auto *repeatRow = new QHBoxLayout();
    m_repeatCheck = new QCheckBox(QStringLiteral("Repeat send every"), panel);
    m_repeatIntervalEdit = new QLineEdit(QStringLiteral("1000"), panel);
    m_repeatIntervalEdit->setFixedWidth(72);
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
    repeatRow->addWidget(new QLabel(QStringLiteral("ms (repeats the last Send direction)"), panel));
    repeatRow->addStretch(1);
    layout->addLayout(repeatRow);

    connect(m_console, &ConsoleView::countsChanged, this, &MainWindow::updateCounts);
    connect(m_console, &ConsoleView::selectionChars, this,
            [this](int chars) { m_selLabel->setText(QStringLiteral("Sel: %1").arg(chars)); });

    // Push initial display options into the console.
    applyFormats();
    applyNewlineMode();

    return panel;
}

void MainWindow::populateDevices() {
    const QString previous = m_deviceBox->currentText();
    m_deviceBox->clear();

    // Character-device tty nodes are "system" entries; include AllEntries so the
    // name filters match them reliably across Qt versions. Natural sort keeps
    // ttyS2 before ttyS10.
    const QDir dev(QStringLiteral("/dev"));
    const QStringList filters{QStringLiteral("ttyUSB*"), QStringLiteral("ttyACM*"), QStringLiteral("ttyS*"), QStringLiteral("ttyAMA*")};
    const auto flags = QDir::System | QDir::AllEntries | QDir::NoDotAndDotDot;
    const QStringList found = dev.entryList(filters, flags, QDir::Name | QDir::LocaleAware);
    for (const QString &name : found) {
        m_deviceBox->addItem(QStringLiteral("/dev/") + name);
    }

    // Stable by-id symlinks for USB adapters, when present.
    const QDir byId(QStringLiteral("/dev/serial/by-id"));
    if (byId.exists()) {
        for (const QString &name : byId.entryList(QDir::System | QDir::NoDotAndDotDot)) {
            m_deviceBox->addItem(byId.filePath(name));
        }
    }

    if (m_deviceBox->count() == 0) {
        m_deviceBox->addItem(QStringLiteral("/dev/ttyUSB0"));
    }
    if (!previous.isEmpty()) {
        m_deviceBox->setCurrentText(previous);
    }
}

void MainWindow::toggleProxy() {
    if (m_proxy->isRunning()) {
        m_proxy->close();
        return;
    }

    const int baud = m_baudBox->currentText().toInt();
    if (baud <= 0) {
        onError(QStringLiteral("Invalid baud rate: %1").arg(m_baudBox->currentText()));
        return;
    }

    SerialConfig cfg;
    cfg.device = m_deviceBox->currentText();
    cfg.baud = baud;
    cfg.dataBits = m_dataBitsBox->currentData().toInt();
    cfg.parity = m_parityBox->currentData().toChar().toLatin1();
    cfg.stopBits = m_stopBitsBox->currentData().toInt();
    cfg.flow = static_cast<FlowControl>(m_flowBox->currentData().toInt());
    cfg.symlinkPath = m_symlinkEdit->text().trimmed();
    cfg.directMode = m_directCheck->isChecked();

    m_lastConfig = cfg;  // remembered for auto-reconnect
    if (m_reconnectTimer != nullptr) {
        m_reconnectTimer->stop();
    }

    // Save configuration settings
    {
        QSettings settings;
        settings.setValue(QStringLiteral("connection/device"), cfg.device);
        settings.setValue(QStringLiteral("connection/baud"), cfg.baud);
        settings.setValue(QStringLiteral("connection/dataBits"), cfg.dataBits);
        settings.setValue(QStringLiteral("connection/parity"), m_parityBox->currentText());
        settings.setValue(QStringLiteral("connection/stopBits"), cfg.stopBits);
        settings.setValue(QStringLiteral("connection/flow"), m_flowBox->currentText());
        settings.setValue(QStringLiteral("connection/symlink"), cfg.symlinkPath);
        settings.setValue(QStringLiteral("connection/directMode"), cfg.directMode);
    }

    m_proxy->open(cfg);
}

void MainWindow::onStarted(const QString &slavePath) {
    setRunningState(true);
    if (m_directCheck->isChecked()) {
        m_statusLabel->setText(QStringLiteral("Connected directly to: <b>%1</b>").arg(m_deviceBox->currentText()));
    } else {
        m_statusLabel->setText(QStringLiteral("Intercepting. Point the target app at: <b>%1</b>").arg(slavePath));
    }
    if (m_modemTimer != nullptr) {
        m_modemTimer->start();
        pollModemLines();
    }
}

void MainWindow::onStopped() {
    setRunningState(false);
    m_statusLabel->setText(QStringLiteral("Stopped."));
    if (m_modemTimer != nullptr) {
        m_modemTimer->stop();
    }
}

void MainWindow::onError(const QString &message) {
    m_statusLabel->setText(QStringLiteral("<span style='color:#e57373'>%1</span>").arg(message));
}

void MainWindow::setRunningState(bool running) {
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
    if (running) {
        m_toAppBtn->setEnabled(!m_directCheck->isChecked());
    } else {
        m_toAppBtn->setEnabled(true);
    }
}

void MainWindow::applyFormats() {
    m_console->setFormats(m_hexCheck->isChecked(), m_decCheck->isChecked(), m_binCheck->isChecked(), m_asciiCheck->isChecked());
}

void MainWindow::applyNewlineMode() {
    const int idx = m_newlineModeBox->currentIndex();
    const bool needsParam = idx != 0;
    m_newlineParamEdit->setEnabled(needsParam);

    auto mode = ConsoleView::NewlineMode::PerChunk;
    int param = 0;
    if (idx == 1) {
        mode = ConsoleView::NewlineMode::Delimiter;
        bool ok = false;
        param = m_newlineParamEdit->text().toInt(&ok, 16);
        if (!ok) {
            param = 0x0A;
        }
    } else if (idx == 2) {
        mode = ConsoleView::NewlineMode::FixedCount;
        bool ok = false;
        param = m_newlineParamEdit->text().toInt(&ok, 10);
        if (!ok || param <= 0) {
            param = 16;
        }
    }
    m_console->setNewlineMode(mode, param);
}

void MainWindow::updateCounts(qint64 rx, qint64 tx) {
    m_countsLabel->setText(QStringLiteral("Rx: %1   Tx: %2").arg(rx).arg(tx));
}

QByteArray MainWindow::encodeInjection(bool &ok) {
    ok = true;
    QByteArray bytes;
    const QString text = m_injectEdit->text();
    int errPos = -1;
    switch (m_injectFormatBox->currentIndex()) {
        case 1:  // ASCII
            bytes = text.toUtf8();
            break;
        case 2:  // DEC
            if (!codec::parseDecString(text, bytes, &errPos)) {
                onError(QStringLiteral("Invalid decimal token at position %1").arg(errPos + 1));
                ok = false;
                return {};
            }
            break;
        case 3:  // BIN
            if (!codec::parseBinString(text, bytes, &errPos)) {
                onError(QStringLiteral("Invalid binary token at position %1").arg(errPos + 1));
                ok = false;
                return {};
            }
            break;
        default:  // HEX
            if (!codec::parseHexString(text, bytes, &errPos)) {
                onError(QStringLiteral("Invalid hex token at position %1").arg(errPos + 1));
                ok = false;
                return {};
            }
            break;
    }
    switch (m_injectEndingBox->currentIndex()) {
        case 1:
            bytes.append('\r');
            break;
        case 2:
            bytes.append('\n');
            break;
        case 3:
            bytes.append('\r');
            bytes.append('\n');
            break;
        default:
            break;
    }
    return bytes;
}

void MainWindow::sendInjection(bool toDevice) {
    m_repeatToDevice = toDevice;  // remembered so the repeat timer reuses it
    bool ok = false;
    const QByteArray bytes = encodeInjection(ok);
    if (!ok || bytes.isEmpty()) {
        return;
    }
    if (toDevice) {
        m_proxy->injectToDevice(bytes);
    } else {
        m_proxy->injectToApp(bytes);
    }
}

void MainWindow::doFind(bool backward) {
    const QString query = m_findEdit->text();
    if (query.isEmpty()) {
        return;
    }
    QTextDocument::FindFlags flags;
    if (backward) {
        flags |= QTextDocument::FindBackward;
    }
    if (!m_console->find(query, flags)) {
        // Wrap around to the other end and retry once.
        QTextCursor cursor = m_console->textCursor();
        cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        m_console->setTextCursor(cursor);
        m_console->find(query, flags);
    }
}

void MainWindow::saveReceived() {
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
    m_statusLabel->setText(QStringLiteral("Saved capture to %1").arg(path));
}

void MainWindow::toggleLogging() {
    if (m_console->isLogging()) {
        m_console->stopLogging();
        m_logBtn->setChecked(false);
        m_statusLabel->setText(QStringLiteral("Logging stopped."));
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
    m_statusLabel->setText(QStringLiteral("Logging to %1").arg(path));
}

void MainWindow::sendFile() {
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
        m_proxy->injectToDevice(bytes);
        m_statusLabel->setText(QStringLiteral("Sent %1 bytes from %2").arg(bytes.size()).arg(path));
    }
}

void MainWindow::pollModemLines() {
    const PtyProxy::ModemLines lines = m_proxy->modemLines();
    const auto paint = [](QLabel *led, bool on) {
        led->setStyleSheet(on ? QStringLiteral("color:#66bb6a;font-weight:bold") : QStringLiteral("color:#555"));
    };
    paint(m_ctsLed, lines.cts);
    paint(m_dsrLed, lines.dsr);
    paint(m_dcdLed, lines.dcd);
    paint(m_riLed, lines.ri);
}

void MainWindow::onDisconnected() {
    setRunningState(false);
    if (m_modemTimer != nullptr) {
        m_modemTimer->stop();
    }
    if (m_reconnectCheck != nullptr && m_reconnectCheck->isChecked()) {
        m_statusLabel->setText(QStringLiteral("<span style='color:#e57373'>Device lost — reconnecting…</span>"));
        m_reconnectTimer->start(1000);
    } else {
        m_statusLabel->setText(QStringLiteral("<span style='color:#e57373'>Device disconnected.</span>"));
    }
}

QWidget *MainWindow::buildSignalPanel(QWidget *parent) {
    auto *group = new QGroupBox(QStringLiteral("Signal lines"), parent);
    auto *row = new QHBoxLayout(group);

    m_rtsCheck = new QCheckBox(QStringLiteral("RTS"), group);
    m_dtrCheck = new QCheckBox(QStringLiteral("DTR"), group);
    connect(m_rtsCheck, &QCheckBox::toggled, this, [this](bool on) { (void)m_proxy->setRts(on); });
    connect(m_dtrCheck, &QCheckBox::toggled, this, [this](bool on) { (void)m_proxy->setDtr(on); });
    auto *breakBtn = new QPushButton(QStringLiteral("Break"), group);
    connect(breakBtn, &QPushButton::clicked, this, [this] { (void)m_proxy->sendBreak(); });

    row->addWidget(new QLabel(QStringLiteral("Output:"), group));
    row->addWidget(m_rtsCheck);
    row->addWidget(m_dtrCheck);
    row->addWidget(breakBtn);
    row->addSpacing(16);

    row->addWidget(new QLabel(QStringLiteral("Input:"), group));
    m_ctsLed = new QLabel(QStringLiteral("CTS"), group);
    m_dsrLed = new QLabel(QStringLiteral("DSR"), group);
    m_dcdLed = new QLabel(QStringLiteral("DCD"), group);
    m_riLed = new QLabel(QStringLiteral("RI"), group);
    for (QLabel *led : {m_ctsLed, m_dsrLed, m_dcdLed, m_riLed}) {
        led->setStyleSheet(QStringLiteral("color:#555"));
        row->addWidget(led);
    }

    row->addStretch(1);
    m_reconnectCheck = new QCheckBox(QStringLiteral("Auto-reconnect"), group);
    row->addWidget(m_reconnectCheck);

    // Poll the modem status lines a few times a second while connected.
    m_modemTimer = new QTimer(this);
    m_modemTimer->setInterval(250);
    connect(m_modemTimer, &QTimer::timeout, this, &MainWindow::pollModemLines);

    // Retry opening the device while auto-reconnect is armed.
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

    return group;
}

}  // namespace aether
