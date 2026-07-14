#include "gui/mainwindow.hpp"

#include "gui/sessions/can_session_widget.hpp"
#include "gui/sessions/session_widget.hpp"
#ifdef AETHER_HAVE_ETHERNET
#include "gui/sessions/ethernet_session_widget.hpp"
#endif
#include "gui/sessions/usb_session_widget.hpp"
#include "gui/common/theme_controller.hpp"
#include "gui/control/control_bridge.hpp"
#include "gui/control/control_server.hpp"
#include "gui/dialogs/welcome_tutorial_dialog.hpp"
#include "aether/version.h"

#include <QThread>
#include <QSignalBlocker>

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QTabWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QSplitter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QStatusBar>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>

namespace aether {

namespace {
QString sessionTypeKey(SessionType type) {
    switch (type) {
        case SessionType::Can:
            return QStringLiteral("can");
        case SessionType::Ethernet:
            return QStringLiteral("ethernet");
        case SessionType::Usb:
            return QStringLiteral("usb");
        case SessionType::Serial:
            break;
    }
    return QStringLiteral("serial");
}

SessionType sessionTypeFromKey(const QString &key) {
    if (key == QStringLiteral("can")) {
        return SessionType::Can;
    }
    if (key == QStringLiteral("ethernet")) {
        return SessionType::Ethernet;
    }
    if (key == QStringLiteral("usb")) {
        return SessionType::Usb;
    }
    return SessionType::Serial;
}
}  // namespace

MainWindow::MainWindow(bool enableControl, QWidget *parent) : QMainWindow(parent) {
    m_theme = new ThemeController(qApp, this);
    QSettings settings;
    m_theme->setMode(ThemeController::modeFromString(settings.value(QStringLiteral("ui/theme")).toString()));

    buildUi();

    // Setup Menu Bar
    QMenuBar *menu = menuBar();

    QMenu *fileMenu = menu->addMenu(tr("&File"));

    QAction *newSessionAct = fileMenu->addAction(tr("New &Serial Session"));
    newSessionAct->setShortcut(QKeySequence::New);
    connect(newSessionAct, &QAction::triggered, this, &MainWindow::addNewSession);

    QAction *newCanSessionAct = fileMenu->addAction(tr("New &CAN Session"));
    connect(newCanSessionAct, &QAction::triggered, this, &MainWindow::addNewCanSession);

#ifdef AETHER_HAVE_ETHERNET
    QAction *newEthernetSessionAct = fileMenu->addAction(tr("New &Ethernet Session"));
    connect(newEthernetSessionAct, &QAction::triggered, this, &MainWindow::addNewEthernetSession);
#endif
    QAction *newUsbSessionAct = fileMenu->addAction(tr("New &USB Session"));
    newUsbSessionAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    connect(newUsbSessionAct, &QAction::triggered, this, &MainWindow::addNewUsbSession);

    QAction *closeSessionAct = fileMenu->addAction(tr("&Close Session"));
    closeSessionAct->setShortcut(QKeySequence::Close);
    connect(closeSessionAct, &QAction::triggered, this, &MainWindow::closeCurrentSession);

    fileMenu->addSeparator();
    QAction *exitAct = fileMenu->addAction(tr("E&xit"));
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, this, &QWidget::close);

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

    QMenu *windowMenu = menu->addMenu(tr("&Window"));
    QAction *tileAct = windowMenu->addAction(tr("&Tile Workspace"));
    connect(tileAct, &QAction::triggered, this, &MainWindow::tileWorkspace);
    QAction *resetAct = windowMenu->addAction(tr("&Reset Layout (Stack tabs)"));
    connect(resetAct, &QAction::triggered, this, &MainWindow::resetWorkspaceLayout);

    windowMenu->addSeparator();
    m_controlAction = windowMenu->addAction(tr("Enable &Control Channel"));
    m_controlAction->setCheckable(true);
    m_controlAction->setToolTip(tr("Allow local scripts to send/receive on your sessions over a control socket"));
    connect(m_controlAction, &QAction::toggled, this, &MainWindow::setControlEnabled);

    QMenu *helpMenu = menu->addMenu(tr("&Help"));
    QAction *tutorialAct = helpMenu->addAction(tr("Welcome &Tutorial…"));
    connect(tutorialAct, &QAction::triggered, this, [this]() {
        WelcomeTutorialDialog dlg(this);
        dlg.exec();
    });
    helpMenu->addSeparator();
    QAction *projectPageAct = helpMenu->addAction(tr("AetherBus on &GitHub"));
    connect(projectPageAct, &QAction::triggered, this,
            [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/beratatmaca/AetherBus"))); });
    QAction *issuesAct = helpMenu->addAction(tr("Report an &Issue…"));
    connect(issuesAct, &QAction::triggered, this,
            [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/beratatmaca/AetherBus/issues"))); });
    helpMenu->addSeparator();
    QAction *aboutAct = helpMenu->addAction(tr("&About AetherBus…"));
    connect(aboutAct, &QAction::triggered, this, [this]() {
        auto *dlg = new QDialog(this);
        dlg->setWindowTitle(tr("About AetherBus"));
        dlg->setWindowIcon(QIcon(QStringLiteral(":/aetherbus/icon.ico")));
        dlg->setFixedWidth(440);

        auto *root = new QVBoxLayout(dlg);
        root->setSpacing(12);
        root->setContentsMargins(24, 24, 24, 20);

        // Logo + app name row
        auto *headerRow = new QHBoxLayout();
        auto *logoLbl = new QLabel(dlg);
        logoLbl->setPixmap(QPixmap(QStringLiteral(":/aetherbus/icon.png")).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        logoLbl->setFixedSize(64, 64);
        headerRow->addWidget(logoLbl);

        auto *titleCol = new QVBoxLayout();
        auto *nameLbl = new QLabel(QStringLiteral("<b style='font-size:18pt'>AetherBus</b>"), dlg);
        auto *tagLbl = new QLabel(QStringLiteral("<span style='color:#808080'>Serial Bus Interceptor &amp; Monitor</span>"), dlg);
        titleCol->addWidget(nameLbl);
        titleCol->addWidget(tagLbl);
        titleCol->addStretch();
        headerRow->addSpacing(12);
        headerRow->addLayout(titleCol);
        headerRow->addStretch();
        root->addLayout(headerRow);

        // Separator
        auto *sep = new QFrame(dlg);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        root->addWidget(sep);

        // Details
        auto *body = new QTextBrowser(dlg);
        body->setOpenExternalLinks(true);
        body->setFrameStyle(QFrame::NoFrame);
        body->setReadOnly(true);
        body->setHtml(
            QStringLiteral("<table cellspacing='4' style='font-size:10pt'>"
                           "<tr><td><b>Version</b></td><td>&nbsp;</td><td>%1</td></tr>"
                           "<tr><td><b>Commit</b></td><td>&nbsp;</td><td><code>%2</code></td></tr>"
                           "<tr><td><b>Qt</b></td><td>&nbsp;</td><td>%3</td></tr>"
                           "<tr><td><b>License</b></td><td>&nbsp;</td><td>MIT</td></tr>"
                           "<tr><td><b>Copyright</b></td><td>&nbsp;</td><td>&copy; 2026 AetherBus Project</td></tr>"
                           "</table>"
                           "<p style='margin-top:8px; font-size:9pt; color:#808080'>"
                           "A modern, lightweight serial sniffer &amp; bus monitor."
                           "</p>")
                .arg(QString::fromLatin1(AETHER_VERSION_STRING), QString::fromLatin1(AETHER_GIT_SHA), QString::fromLatin1(qVersion())));
        body->setMaximumHeight(160);
        root->addWidget(body);

        // Buttons
        auto *btns = new QDialogButtonBox(dlg);
        auto *okBtn = btns->addButton(QDialogButtonBox::Ok);
        auto *qtBtn = btns->addButton(tr("About &Qt"), QDialogButtonBox::HelpRole);
        connect(okBtn, &QPushButton::clicked, dlg, &QDialog::accept);
        connect(qtBtn, &QPushButton::clicked, this, &MainWindow::showAboutQt);
        root->addWidget(btns);

        dlg->exec();
        dlg->deleteLater();
    });

    setWindowTitle(QStringLiteral("AetherBus — Serial Interceptor"));
    setWindowIcon(QIcon(QStringLiteral(":/aetherbus/icon.ico")));
    qApp->setWindowIcon(windowIcon());  // propagate to taskbar / dock

    const QRect available = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect();
    QSize preferredSize(1650, 950);
    m_baseMinWidth = 1450;
    m_baseMinHeight = 850;
    if (available.isValid()) {
        m_baseMinWidth = qMin(1450, available.width() - 80);
        m_baseMinHeight = qMin(850, available.height() - 80);
        preferredSize.setWidth(qMin(preferredSize.width(), qMax(m_baseMinWidth, available.width() - 80)));
        preferredSize.setHeight(qMin(preferredSize.height(), qMax(m_baseMinHeight, available.height() - 80)));
    }
    setMinimumSize(m_baseMinWidth, m_baseMinHeight);
    const QByteArray savedGeometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (savedGeometry.isEmpty() || !restoreGeometry(savedGeometry)) {
        resize(preferredSize);
    }

    // Reopen whatever sessions were open last time (idle, not connected), or
    // start with one default Serial session on first run.
    restoreWorkspaceState();

    // Control channel is opt-in: enabled by the --control flag or a remembered
    // Window-menu toggle. setControlEnabled updates the action's checkbox.
    const bool controlOn = enableControl || settings.value(QStringLiteral("control/enabled"), false).toBool();
    if (controlOn) {
        setControlEnabled(true);
    }

    // Trigger welcome tutorial if requested
    QTimer::singleShot(100, this, [this]() {
        QSettings settings;
        if (settings.value(QStringLiteral("ui/show_tutorial"), true).toBool()) {
            WelcomeTutorialDialog dlg(this);
            dlg.exec();
        }
    });
}

MainWindow::~MainWindow() {
    for (const auto &session : m_sessions) {
        if (session) {
            session->disconnect(this);
        }
    }
    setControlEnabled(false);  // join the control thread cleanly
}

void MainWindow::setControlEnabled(bool on) {
    if (on == (m_control != nullptr)) {
        // Keep the menu checkbox in sync even on a redundant call.
        if (m_controlAction != nullptr && m_controlAction->isChecked() != on) {
            const QSignalBlocker block(m_controlAction);
            m_controlAction->setChecked(on);
        }
        return;
    }

    if (on) {
        m_controlBridge = new ControlBridge(this);  // GUI thread
        m_controlBridge->setWindow(this);
        m_controlThread = new QThread(this);
        m_control = new ControlServer();  // no parent — moved to its own thread
        m_control->setBridge(m_controlBridge);
        m_control->moveToThread(m_controlThread);
        // listen() must run on the worker thread that owns the QLocalServer.
        connect(m_controlThread, &QThread::started, m_control, &ControlServer::start);
        // Cross-thread (queued) traffic hand-off, GUI -> control thread.
        connect(m_controlBridge, &ControlBridge::traffic, m_control, &ControlServer::onTraffic);
        m_controlThread->start();

        // Register every currently-open session (ids were assigned in addSession).
        for (SessionView *session : m_sessions) {
            if (session != nullptr) {
                m_controlBridge->registerSession(session);
            }
        }
    } else {
        if (m_control != nullptr) {
            // Close the socket on its own thread, then join.
            QMetaObject::invokeMethod(m_control, "stop", Qt::BlockingQueuedConnection);
        }
        if (m_controlThread != nullptr) {
            m_controlThread->quit();
            m_controlThread->wait();
            delete m_controlThread;  // thread joined; safe to delete it and its worker
            m_controlThread = nullptr;
        }
        delete m_control;  // safe: worker thread has stopped
        m_control = nullptr;
        delete m_controlBridge;
        m_controlBridge = nullptr;
    }

    if (m_controlAction != nullptr && m_controlAction->isChecked() != on) {
        const QSignalBlocker block(m_controlAction);
        m_controlAction->setChecked(on);
    }
    QSettings().setValue(QStringLiteral("control/enabled"), on);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveWorkspaceState();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveWorkspaceState() {
    m_sessions.removeAll(nullptr);
    QSettings settings;

    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/tiledMode"), m_tiledMode);
    settings.setValue(QStringLiteral("window/activeIndex"), m_tabWidget ? m_tabWidget->currentIndex() : -1);

    settings.remove(QStringLiteral("sessions"));
    settings.beginWriteArray(QStringLiteral("sessions"));
    int index = 0;
    for (const auto &session : m_sessions) {
        if (!session) {
            continue;
        }
        settings.setArrayIndex(index++);
        settings.setValue(QStringLiteral("type"), sessionTypeKey(session->sessionType()));
        session->saveSettings(settings);
    }
    settings.endArray();
}

void MainWindow::restoreWorkspaceState() {
    QSettings settings;
    m_tiledMode = settings.value(QStringLiteral("window/tiledMode"), false).toBool();

    const int count = settings.beginReadArray(QStringLiteral("sessions"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        const SessionType type = sessionTypeFromKey(settings.value(QStringLiteral("type")).toString());
        addSession(type);
        if (SessionView *session = m_sessions.isEmpty() ? nullptr : m_sessions.last()) {
            session->loadSettings(settings);
        }
    }
    settings.endArray();

    if (count == 0) {
        addNewSession();
        return;
    }

    const int activeIndex = settings.value(QStringLiteral("window/activeIndex"), 0).toInt();
    if (!m_tiledMode && m_tabWidget && activeIndex >= 0 && activeIndex < m_tabWidget->count()) {
        m_tabWidget->setCurrentIndex(activeIndex);
    }
}

void MainWindow::buildUi() {
    m_stack = new QStackedWidget(this);

    m_dashboard = new QWidget(this);
    auto *dashLayout = new QVBoxLayout(m_dashboard);
    dashLayout->setAlignment(Qt::AlignCenter);
    dashLayout->setContentsMargins(40, 40, 40, 40);
    dashLayout->setSpacing(20);

    auto *logoLabel = new QLabel(m_dashboard);
    QPixmap logoPix(QStringLiteral(":/aetherbus/icon.png"));
    if (!logoPix.isNull()) {
        logoLabel->setPixmap(logoPix.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    logoLabel->setAlignment(Qt::AlignCenter);
    dashLayout->addWidget(logoLabel);

    auto *welcomeLabel = new QLabel(QStringLiteral("<b style='font-size:16pt'>AetherBus Workspace</b>"), m_dashboard);
    welcomeLabel->setAlignment(Qt::AlignCenter);
    dashLayout->addWidget(welcomeLabel);

    auto *infoLabel = new QLabel(
        tr("Open multiple Serial, CAN, or Ethernet sessions side-by-side.\nUse the Window menu to Tile or Reset the workspace layout."),
        m_dashboard);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setStyleSheet(QStringLiteral("color:#808080; font-size:11pt;"));
    dashLayout->addWidget(infoLabel);

    m_stack->addWidget(m_dashboard);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setDocumentMode(true);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    m_stack->addWidget(m_tabWidget);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_stack->addWidget(m_splitter);

    setCentralWidget(m_stack);
    m_stack->setCurrentWidget(m_dashboard);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_statusLabel, 1);

    m_statusClearTimer = new QTimer(this);
    m_statusClearTimer->setSingleShot(true);
    connect(m_statusClearTimer, &QTimer::timeout, this, [this] { showStatus(QString(), false); });
}

void MainWindow::showStatus(const QString &text, bool isError) {
    if (text.isEmpty()) {
        m_statusClearTimer->stop();
        m_statusLabel->clear();
        m_statusLabel->setStyleSheet(QString());
        return;
    }
    if (isError) {
        m_statusLabel->setStyleSheet(QStringLiteral("color:#e57373; font-weight:bold;"));
        m_statusLabel->setText(QStringLiteral("⚠  ") + text);
        m_statusClearTimer->stop();  // errors persist until the next message
    } else {
        m_statusLabel->setStyleSheet(QString());  // themed default colour
        m_statusLabel->setText(text);
        m_statusClearTimer->start(5000);  // info fades after a few seconds
    }
}

void MainWindow::addNewSession() {
    addSession(SessionType::Serial);
}

void MainWindow::addNewCanSession() {
    addSession(SessionType::Can);
}

#ifdef AETHER_HAVE_ETHERNET
void MainWindow::addNewEthernetSession() {
    addSession(SessionType::Ethernet);
}
#endif

void MainWindow::addNewUsbSession() {
    addSession(SessionType::Usb);
}

void MainWindow::addSession(SessionType type) {
    SessionView *session = nullptr;
    QString title;
    m_sessions.removeAll(nullptr);
    const int count = static_cast<int>(m_sessions.size()) + 1;
    if (type == SessionType::Can) {
        session = new CanSessionWidget(this);
        title = QStringLiteral("CAN Session %1").arg(count);
#ifdef AETHER_HAVE_ETHERNET
    } else if (type == SessionType::Ethernet) {
        session = new EthernetSessionWidget(this);
        title = QStringLiteral("Ethernet Session %1").arg(count);
#endif
    } else if (type == SessionType::Usb) {
        session = new UsbSessionWidget(this);
        title = QStringLiteral("USB Session %1").arg(count);
    } else {
        session = new SessionWidget(this);
        title = QStringLiteral("Serial Session %1").arg(count);
    }

    session->setObjectName(title);
    session->setAttribute(Qt::WA_DeleteOnClose);
    m_sessions.append(session);

    const int controlId = ++m_nextControlId;
    session->setControlId(controlId);
    if (m_controlBridge != nullptr) {
        m_controlBridge->registerSession(session);
    }

    connect(session, &SessionView::sessionTitleChanged, this, [this, session](const QString &newTitle) {
        int idx = m_tabWidget->indexOf(session);
        if (idx != -1) {
            m_tabWidget->setTabText(idx, newTitle);
        }
        session->setObjectName(newTitle);
    });

    connect(session, &SessionView::statusMessage, this, [this, session](const QString &text, bool isError) {
        m_sessionStatus.insert(session, {text, isError});
        int idx = m_tabWidget->indexOf(session);
        QString prefix = (idx != -1) ? m_tabWidget->tabText(idx) : QString();
        showStatus(prefix.isEmpty() ? text : QStringLiteral("[%1] %2").arg(prefix, text), isError);
    });

    connect(session, &QObject::destroyed, this, [this, session, controlId]() {
        m_sessionStatus.remove(session);
        m_sessions.removeAll(session);
        if (m_controlBridge != nullptr) {
            m_controlBridge->unregisterSession(controlId);
        }
        if (m_stack && m_dashboard && m_sessions.isEmpty()) {
            m_stack->setCurrentWidget(m_dashboard);
        } else if (m_tiledMode && !m_sessions.isEmpty()) {
            QTimer::singleShot(0, this, [this]() {
                if (m_tiledMode && !m_sessions.isEmpty()) {
                    tileWorkspace();
                }
            });
        }
    });

    m_tabWidget->addTab(session, title);

    if (m_tiledMode) {
        tileWorkspace();
    } else {
        m_stack->setCurrentWidget(m_tabWidget);
        m_tabWidget->setCurrentWidget(session);
    }
}

int MainWindow::createControlSession(SessionType type, QString *error) {
#ifndef AETHER_HAVE_ETHERNET
    if (type == SessionType::Ethernet) {
        if (error != nullptr) {
            *error = QStringLiteral("Ethernet support is not built into this binary");
        }
        return 0;
    }
#endif
    addSession(type);
    m_sessions.removeAll(nullptr);
    if (m_sessions.isEmpty() || m_sessions.last() == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("session creation failed");
        }
        return 0;
    }
    return m_sessions.last()->controlId();
}

void MainWindow::closeCurrentSession() {
    m_sessions.removeAll(nullptr);
    if (m_tiledMode) {
        for (const auto &session : m_sessions) {
            if (session && session->isAncestorOf(QApplication::focusWidget())) {
                session->close();
                return;
            }
        }
        if (!m_sessions.isEmpty() && m_sessions.last()) {
            m_sessions.last()->close();
        }
    } else {
        int idx = m_tabWidget->currentIndex();
        if (idx != -1) {
            QWidget *w = m_tabWidget->widget(idx);
            if (w) {
                w->close();
            }
        }
    }
}

void MainWindow::showAboutQt() {
    QApplication::aboutQt();
}

QWidget *MainWindow::wrapForTile(SessionView *session) {
    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(container);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(6, 2, 4, 2);
    headerLayout->setSpacing(4);

    auto *titleLabel = new QLabel(session->objectName(), header);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch(1);

    auto *closeBtn = new QPushButton(QStringLiteral("✕"), header);
    closeBtn->setObjectName(QStringLiteral("tileCloseButton"));
    closeBtn->setFixedSize(20, 20);
    closeBtn->setFlat(true);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip(tr("Close this session"));
    connect(closeBtn, &QPushButton::clicked, session, &SessionView::close);
    headerLayout->addWidget(closeBtn);

    // Rebuilt fresh every retile, so no stale-connection risk from the old
    // header outliving this one.
    connect(session, &SessionView::sessionTitleChanged, titleLabel, &QLabel::setText);

    layout->addWidget(header);
    layout->addWidget(session, 1);
    return container;
}

int MainWindow::gridColumnCount(int n) {
    int cols = 1;
    while (cols * cols < n) {
        ++cols;
    }
    return cols;
}

QSplitter *MainWindow::buildGridSplitter(const QList<SessionView *> &sessions) {
    const int n = static_cast<int>(sessions.size());
    const int cols = gridColumnCount(n);

    const int base = n / cols;
    const int extra = n % cols;

    auto *root = new QSplitter(Qt::Horizontal);
    root->setObjectName(QStringLiteral("workspaceGridRoot"));
    root->setChildrenCollapsible(false);

    int idx = 0;
    for (int c = 0; c < cols && idx < n; ++c) {
        const int rowsInCol = base + (c < extra ? 1 : 0);
        if (rowsInCol <= 1) {
            SessionView *session = sessions[idx++];
            session->show();
            root->addWidget(wrapForTile(session));
            continue;
        }
        auto *column = new QSplitter(Qt::Vertical, root);
        column->setObjectName(QStringLiteral("workspaceGridColumn"));
        column->setChildrenCollapsible(false);
        for (int r = 0; r < rowsInCol && idx < n; ++r) {
            SessionView *session = sessions[idx++];
            session->show();
            column->addWidget(wrapForTile(session));
        }
        for (int i = 0; i < column->count(); ++i) {
            column->setStretchFactor(i, 1);
        }
        root->addWidget(column);
    }

    for (int i = 0; i < root->count(); ++i) {
        root->setStretchFactor(i, 1);
    }
    return root;
}

void MainWindow::tileWorkspace() {
    m_sessions.removeAll(nullptr);
    if (m_sessions.isEmpty()) {
        return;
    }
    m_tiledMode = true;

    for (const auto &session : m_sessions) {
        if (session) {
            session->setParent(nullptr);
        }
    }

    while (m_tabWidget->count() > 0) {
        m_tabWidget->removeTab(0);
    }

    m_stack->removeWidget(m_splitter);
    delete m_splitter;

    m_splitter = buildGridSplitter(m_sessions);
    m_stack->addWidget(m_splitter);
    m_stack->setCurrentWidget(m_splitter);
    updateMinimumSizeForTiling();
}

void MainWindow::updateMinimumSizeForTiling() {
    if (!m_tiledMode || m_sessions.isEmpty()) {
        setMinimumSize(m_baseMinWidth, m_baseMinHeight);
        return;
    }

    const int n = static_cast<int>(m_sessions.size());
    const int cols = gridColumnCount(n);
    const int rows = (n + cols - 1) / cols;

    // Header row wrapForTile() adds above every tile (title + close button).
    constexpr int kTileHeaderHeight = 28;

    int tileMinWidth = 0;
    int tileMinHeight = 0;
    for (const auto &session : m_sessions) {
        if (!session) {
            continue;
        }
        const QSize hint = session->sizeHint();
        tileMinWidth = qMax(tileMinWidth, hint.width());
        tileMinHeight = qMax(tileMinHeight, hint.height());
    }
    tileMinHeight += kTileHeaderHeight;

    int wantWidth = cols * tileMinWidth;
    int wantHeight = rows * tileMinHeight;

    const QRect available = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect();
    if (available.isValid()) {
        wantWidth = qMin(wantWidth, available.width() - 80);
        wantHeight = qMin(wantHeight, available.height() - 80);
    }

    setMinimumSize(qMax(m_baseMinWidth, wantWidth), qMax(m_baseMinHeight, wantHeight));
}

void MainWindow::resetWorkspaceLayout() {
    m_sessions.removeAll(nullptr);
    m_tiledMode = false;

    for (const auto &session : m_sessions) {
        if (session) {
            session->setParent(nullptr);
        }
    }

    m_stack->removeWidget(m_splitter);
    delete m_splitter;
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_stack->addWidget(m_splitter);

    for (const auto &session : m_sessions) {
        if (session) {
            m_tabWidget->addTab(session, session->objectName().isEmpty() ? QStringLiteral("Session") : session->objectName());
            session->show();
        }
    }

    m_stack->setCurrentWidget(m_tabWidget);
    updateMinimumSizeForTiling();
}

void MainWindow::onTabCloseRequested(int index) {
    if (index >= 0 && index < m_tabWidget->count()) {
        QWidget *w = m_tabWidget->widget(index);
        if (w) {
            w->close();
        }
    }
}

}  // namespace aether
