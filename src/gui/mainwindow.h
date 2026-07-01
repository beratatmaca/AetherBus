#pragma once

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
    void addNewSession();
    void closeSessionTab(int index);
    void closeCurrentSession();

private:
    void buildUi();

    QTabWidget *m_tabWidget = nullptr;
    ThemeController *m_theme = nullptr;
};

}  // namespace aether
