#pragma once

#include "gui/sessions/session_view.hpp"

#include <QHash>
#include <QMainWindow>
#include <QString>

class QTabWidget;
class QLabel;
class QTimer;

namespace aether {

class ThemeController;

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
    void closeSessionTab(int index);
    void closeCurrentSession();
    void showAboutQt();

private:
    void buildUi();
    void addSession(SessionType type);

    /// Show a message in the bottom status bar. Errors persist (red, ⚠); other
    /// messages auto-clear after a few seconds. Empty text clears the bar.
    void showStatus(const QString &text, bool isError);

    /// Last status reported by each session, so switching tabs restores it.
    struct StatusEntry {
        QString text;
        bool isError = false;
    };

    QTabWidget *m_tabWidget = nullptr;
    ThemeController *m_theme = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTimer *m_statusClearTimer = nullptr;
    QHash<QObject *, StatusEntry> m_sessionStatus;
};

}  // namespace aether
