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
class ControlServer;
class ControlBridge;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Construct the main window.
     * @param enableControl force-enable the localhost control channel at startup
     *        (the @c --control CLI flag); it is otherwise off unless the saved
     *        Window-menu toggle turns it on.
     * @param parent Optional parent widget.
     */
    explicit MainWindow(bool enableControl = false, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    /** @brief New serial session (Ctrl+N). */
    void addNewSession();
    /** @brief New SocketCAN session. */
    void addNewCanSession();
#ifdef AETHER_HAVE_ETHERNET
    /** @brief New Ethernet/IP session. */
    void addNewEthernetSession();
#endif
    /** @brief New USB session (Ctrl+U). */
    void addNewUsbSession();
    void closeCurrentSession();
    void showAboutQt();
    void tileWorkspace();
    void resetWorkspaceLayout();
    void onTabCloseRequested(int index);

    /**
     * @brief Persist window geometry, layout mode, active tab, and every open
     * session's configuration.
     *
     * Called from @ref closeEvent; kept invokable
     * (like the slots above) so tests can drive it directly.
     */
    void saveWorkspaceState();
    /**
     * @brief Recreate the sessions saved by @ref saveWorkspaceState, idle (not
     * connected).
     *
     * Falls back to a single default Serial session if there's
     * nothing saved (first run). Called once from the constructor.
     */
    void restoreWorkspaceState();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void addSession(SessionType type);

    /**
     * @brief Arrange sessions into a balanced grid of nested splitters
     * (cols = ceil(sqrt(n)), extra rows assigned to the first columns).
     */
    QSplitter *buildGridSplitter(const QList<SessionView *> &sessions);

    /**
     * @brief Number of grid columns for @p n tiles (ceil(sqrt(n))), shared by
     * @ref buildGridSplitter and @ref updateMinimumSizeForTiling so the two
     * never drift apart.
     */
    static int gridColumnCount(int n);

    /**
     * @brief Re-derive the window's minimum size from the current tile grid and
     * each tile's own sizeHint(); falls back to the fixed startup minimum
     * while tabbed/untiled.
     */
    void updateMinimumSizeForTiling();

    /**
     * @brief Wrap a session in a thin header (title + close button) for tiled mode,
     * where there's no tab bar to provide a close affordance otherwise.
     */
    QWidget *wrapForTile(SessionView *session);

    /**
     * @brief Show a message in the bottom status bar.
     *
     * Errors persist (red, ⚠); other
     * messages auto-clear after a few seconds. Empty text clears the bar.
     */
    void showStatus(const QString &text, bool isError);

    /** @brief Last status reported by each session, so switching tabs restores it. */
    struct StatusEntry {
        QString text;
        bool isError = false;
    };

    QPointer<QStackedWidget> m_stack;
    QPointer<QWidget> m_dashboard;
    QPointer<QTabWidget> m_tabWidget;
    QPointer<QSplitter> m_splitter;
    QList<SessionView *> m_sessions;
    bool m_tiledMode = false;
    int m_baseMinWidth = 0;
    int m_baseMinHeight = 0;

    ThemeController *m_theme = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTimer *m_statusClearTimer = nullptr;
    QHash<QObject *, StatusEntry> m_sessionStatus;

    /**
     * @brief Enable/disable the localhost control channel at runtime.
     *
     * Starts (or stops + joins) the worker thread that owns the control socket,
     * registers/unregisters all open sessions, updates the menu action, and
     * persists the choice to QSettings.
     */
    void setControlEnabled(bool on);

    QThread *m_controlThread = nullptr;    ///< Owns the control socket off the GUI thread.
    ControlServer *m_control = nullptr;    ///< Lives on @ref m_controlThread; null when disabled.
    ControlBridge *m_controlBridge = nullptr;  ///< GUI-thread session-access half; null when disabled.
    QAction *m_controlAction = nullptr;    ///< Window-menu "Enable Control Channel" toggle.
    int m_nextControlId = 0;               ///< Monotonic id source for session registration.
};

}  // namespace aether
