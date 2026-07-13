#pragma once

#include "gui/sessions/session_view.hpp"

#include <QHash>
#include <QMainWindow>
#include <QString>
#include <QDockWidget>
#include <QCloseEvent>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QPointer>

class QLabel;
class QTimer;
class QStackedWidget;
class QSplitter;
class QTabWidget;

namespace aether {

class ThemeController;
class SessionView;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void addNewSession();     ///< New serial session (Ctrl+N).
    void addNewCanSession();  ///< New SocketCAN session.
#ifdef AETHER_HAVE_ETHERNET
    void addNewEthernetSession(); ///< New Ethernet/IP session.
#endif
    void closeCurrentSession();
    void showAboutQt();
    void tileWorkspace();
    void resetWorkspaceLayout();
    void onTabCloseRequested(int index);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void addSession(SessionType type);

    /// Arrange sessions into a balanced grid of nested splitters
    /// (cols = ceil(sqrt(n)), extra rows assigned to the first columns).
    QSplitter *buildGridSplitter(const QList<QPointer<SessionView>> &sessions);

    /// Persist window geometry, layout mode, active tab, and every open
    /// session's configuration. Called from @ref closeEvent.
    void saveWorkspaceState();
    /// Recreate the sessions saved by @ref saveWorkspaceState, idle (not
    /// connected). Falls back to a single default Serial session if there's
    /// nothing saved (first run).
    void restoreWorkspaceState();

    /// Show a message in the bottom status bar. Errors persist (red, ⚠); other
    /// messages auto-clear after a few seconds. Empty text clears the bar.
    void showStatus(const QString &text, bool isError);

    /// Last status reported by each session, so switching tabs restores it.
    struct StatusEntry {
        QString text;
        bool isError = false;
    };

    QPointer<QStackedWidget> m_stack;
    QPointer<QWidget> m_dashboard;
    QPointer<QTabWidget> m_tabWidget;
    QPointer<QSplitter> m_splitter;
    QList<QPointer<SessionView>> m_sessions;
    bool m_tiledMode = false;

    ThemeController *m_theme = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTimer *m_statusClearTimer = nullptr;
    QHash<QObject *, StatusEntry> m_sessionStatus;
};

}  // namespace aether
