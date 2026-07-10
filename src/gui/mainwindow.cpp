#include "gui/mainwindow.hpp"

#include "gui/sessions/can_session_widget.hpp"
#include "gui/sessions/session_widget.hpp"
#ifdef AETHER_HAVE_ETHERNET
#include "gui/sessions/ethernet_session_widget.hpp"
#endif
#include "gui/common/theme_controller.hpp"
#include "aether/version.h"

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
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QStatusBar>
#include <QTimer>

namespace aether {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
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

    QMenu *helpMenu = menu->addMenu(tr("&Help"));
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
        auto *tagLbl = new QLabel(QStringLiteral("<span style='color:#888'>Serial Bus Interceptor &amp; Monitor</span>"), dlg);
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
                           "<p style='margin-top:8px; font-size:9pt; color:#888'>"
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
    int minWidth = 1450;
    int minHeight = 850;
    if (available.isValid()) {
        minWidth = qMin(1450, available.width() - 80);
        minHeight = qMin(850, available.height() - 80);
        preferredSize.setWidth(qMin(preferredSize.width(), qMax(minWidth, available.width() - 80)));
        preferredSize.setHeight(qMin(preferredSize.height(), qMax(minHeight, available.height() - 80)));
    }
    setMinimumSize(minWidth, minHeight);
    resize(preferredSize);

    // Add initial session tab
    addNewSession();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::closeSessionTab);

    // An add button in the corner of the tab bar, offering a session type.
    auto *addTabButton = new QPushButton(QStringLiteral("+"), this);
    addTabButton->setToolTip(tr("Open new session"));
    addTabButton->setFlat(true);
    addTabButton->setFixedWidth(30);
    auto *addMenu = new QMenu(addTabButton);
    addMenu->addAction(tr("Serial Session"), this, &MainWindow::addNewSession);
    addMenu->addAction(tr("CAN Session"), this, &MainWindow::addNewCanSession);
#ifdef AETHER_HAVE_ETHERNET
    addMenu->addAction(tr("Ethernet Session"), this, &MainWindow::addNewEthernetSession);
#endif
    addTabButton->setMenu(addMenu);
    m_tabWidget->setCornerWidget(addTabButton, Qt::TopRightCorner);

    setCentralWidget(m_tabWidget);

    // Always-visible status bar: mirrors the active session's status/errors so a
    // transient message can't be missed in the sidebar.
    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_statusLabel, 1);

    m_statusClearTimer = new QTimer(this);
    m_statusClearTimer->setSingleShot(true);
    connect(m_statusClearTimer, &QTimer::timeout, this, [this] { showStatus(QString(), false); });

    // When the active tab changes, restore that session's last message.
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int) {
        const auto it = m_sessionStatus.constFind(m_tabWidget->currentWidget());
        if (it != m_sessionStatus.constEnd()) {
            showStatus(it->text, it->isError);
        } else {
            showStatus(QString(), false);
        }
    });
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

void MainWindow::addSession(SessionType type) {
    SessionView *session = nullptr;
    QString title;
    const int count = m_tabWidget->count() + 1;
    if (type == SessionType::Can) {
        session = new CanSessionWidget(this);
        title = QStringLiteral("CAN Session %1").arg(count);
#ifdef AETHER_HAVE_ETHERNET
    } else if (type == SessionType::Ethernet) {
        session = new EthernetSessionWidget(this);
        title = QStringLiteral("Ethernet Session %1").arg(count);
#endif
    } else {
        session = new SessionWidget(this);
        title = QStringLiteral("Serial Session %1").arg(count);
    }

    const int index = m_tabWidget->addTab(session, title);

    connect(session, &SessionView::sessionTitleChanged, this, [this, session](const QString &newTitle) {
        const int idx = m_tabWidget->indexOf(session);
        if (idx != -1) {
            m_tabWidget->setTabText(idx, newTitle);
        }
    });

    connect(session, &SessionView::statusMessage, this, [this, session](const QString &text, bool isError) {
        m_sessionStatus.insert(session, {text, isError});
        if (m_tabWidget->currentWidget() == session) {
            showStatus(text, isError);
        }
    });

    m_tabWidget->setCurrentIndex(index);
}

void MainWindow::closeSessionTab(int index) {
    if (index < 0 || index >= m_tabWidget->count()) {
        return;
    }

    auto *session = qobject_cast<SessionView *>(m_tabWidget->widget(index));
    if (!session) {
        return;
    }

    if (session->isRunning()) {
        auto response = QMessageBox::warning(this, tr("Session Running"),
                                             tr("This session is active. Do you want to stop the connection and close the tab?"),
                                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (response == QMessageBox::No) {
            return;
        }
        session->stopSession();
    }

    m_sessionStatus.remove(session);
    m_tabWidget->removeTab(index);
    session->deleteLater();
}

void MainWindow::closeCurrentSession() {
    closeSessionTab(m_tabWidget->currentIndex());
}

void MainWindow::showAboutQt() {
    QApplication::aboutQt();
}

}  // namespace aether
