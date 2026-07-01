#pragma once

#include "gui/sessions/session_view.hpp"

#include <QMainWindow>

class QTabWidget;

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
    void closeSessionTab(int index);
    void closeCurrentSession();
    void showAboutQt();

private:
    void buildUi();
    void addSession(SessionType type);

    QTabWidget *m_tabWidget = nullptr;
    ThemeController *m_theme = nullptr;
};

}  // namespace aether
