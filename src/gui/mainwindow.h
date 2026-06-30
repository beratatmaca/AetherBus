#pragma once

#include <QMainWindow>
#include <QVariantMap>
#include <QTableView>
#include <QTreeView>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QStandardItemModel>
#include "frametablemodel.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QVariantMap& config, QWidget *parent = nullptr);

private slots:
    void toggleCapture();
    void onFrameSelected(const QModelIndex& index);
    void injectSingleMessage();
    void clearTrace();

private:
    void setupUI();
    void updateHTermView(const AetherFrame& frame);
    void updateDetailsTree(const AetherFrame& frame);

    QVariantMap m_config;
    bool m_capturing;
    class SimulatorWorker* m_simulator;

    // Table Model for Zone 1
    FrameTableModel* m_tableModel;

    // Tree Model for Zone 3
    QStandardItemModel* m_treeModel;

    // GUI Widgets
    QTableView* m_traceTableView;
    QTreeView* m_detailsTreeView;

    // HTerm Views (Zone 2)
    QTextEdit* m_htermAscii;
    QTextEdit* m_htermHex;
    QTextEdit* m_htermBin;
    QTextEdit* m_htermDec;

    // Injection Widgets (Zone 4)
    QLineEdit* m_injectIdInput;
    QLineEdit* m_injectDataInput;
    QPushButton* m_injectButton;

    // Toolbar Widgets
    QPushButton* m_captureButton;
    QLineEdit* m_filterBar;
    QLabel* m_statusLabel;
};
