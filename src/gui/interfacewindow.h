#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include "../core/bus_protocol.h"

class InterfaceWindow : public QWidget {
    Q_OBJECT
public:
    explicit InterfaceWindow(QWidget *parent = nullptr);

private slots:
    void refreshInterfaces();
    void onInterfaceSelected(QListWidgetItem *item);
    void configureSelectedInterface();

private:
    QListWidget* m_interfaceListWidget;
    QPushButton* m_configureButton;
    QPushButton* m_refreshButton;
    QLabel* m_statusLabel;
    QList<BusInterfaceInfo> m_availableInterfaces;
};
