#include "gui/mainwindow.h"

#include "gui/session_widget.h"
#include "gui/theme_controller.h"
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
#include <QTabWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextBrowser>

namespace aether {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_theme = new ThemeController(qApp, this);
    QSettings settings;
    m_theme->setMode(ThemeController::modeFromString(settings.value(QStringLiteral("ui/theme")).toString()));

    buildUi();

    // Setup Menu Bar
    QMenuBar *menu = menuBar();

    QMenu *fileMenu = menu->addMenu(tr("&File"));

    QAction *newSessionAct = fileMenu->addAction(tr("&New Session"));
    newSessionAct->setShortcut(QKeySequence::New);
    connect(newSessionAct, &QAction::triggered, this, &MainWindow::addNewSession);

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
        QLabel *logoLbl = new QLabel(dlg);
        logoLbl->setPixmap(QPixmap(QStringLiteral(":/aetherbus/icon.png")).scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        logoLbl->setFixedSize(64, 64);
        headerRow->addWidget(logoLbl);

        auto *titleCol = new QVBoxLayout();
        QLabel *nameLbl = new QLabel(QStringLiteral("<b style='font-size:18pt'>AetherBus</b>"), dlg);
        QLabel *tagLbl = new QLabel(QStringLiteral("<span style='color:#888'>Serial Bus Interceptor &amp; Monitor</span>"), dlg);
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

        // OK button
        auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok, dlg);
        connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
        root->addWidget(btns);

        dlg->exec();
        dlg->deleteLater();
    });

    setWindowTitle(QStringLiteral("AetherBus — Serial Interceptor"));
    setWindowIcon(QIcon(QStringLiteral(":/aetherbus/icon.ico")));
    qApp->setWindowIcon(windowIcon());  // propagate to taskbar / dock

    resize(1000, 720);

    // Add initial session tab
    addNewSession();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::closeSessionTab);

    // An add button in the corner of the tab bar
    auto *addTabButton = new QPushButton(QStringLiteral("+"), this);
    addTabButton->setToolTip(tr("Open new session"));
    addTabButton->setFlat(true);
    addTabButton->setFixedWidth(30);
    connect(addTabButton, &QPushButton::clicked, this, &MainWindow::addNewSession);
    m_tabWidget->setCornerWidget(addTabButton, Qt::TopRightCorner);

    setCentralWidget(m_tabWidget);
}

void MainWindow::addNewSession() {
    auto *session = new SessionWidget(this);

    // Default name
    int count = m_tabWidget->count() + 1;
    QString title = QStringLiteral("Session %1").arg(count);

    int index = m_tabWidget->addTab(session, title);

    connect(session, &SessionWidget::sessionTitleChanged, this, [this, session](const QString &newTitle) {
        int idx = m_tabWidget->indexOf(session);
        if (idx != -1) {
            m_tabWidget->setTabText(idx, newTitle);
        }
    });

    m_tabWidget->setCurrentIndex(index);
}

void MainWindow::closeSessionTab(int index) {
    if (index < 0 || index >= m_tabWidget->count()) {
        return;
    }

    auto *session = qobject_cast<SessionWidget *>(m_tabWidget->widget(index));
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

    m_tabWidget->removeTab(index);
    session->deleteLater();
}

void MainWindow::closeCurrentSession() {
    closeSessionTab(m_tabWidget->currentIndex());
}

}  // namespace aether
