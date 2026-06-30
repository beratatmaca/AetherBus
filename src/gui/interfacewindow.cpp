#include "interfacewindow.h"
#include "configdialog.h"
#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

InterfaceWindow::InterfaceWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("AetherBus — Select Interface");
    resize(480, 360);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* headerLabel = new QLabel("Select Physical or Simulated Interface:", this);
    headerLabel->setStyleSheet("font-weight: bold; font-size: 14px; margin-bottom: 5px;");
    mainLayout->addWidget(headerLabel);

    m_interfaceListWidget = new QListWidget(this);
    mainLayout->addWidget(m_interfaceListWidget);

    connect(m_interfaceListWidget, &QListWidget::itemDoubleClicked, this, &InterfaceWindow::onInterfaceSelected);

    QHBoxLayout* controlLayout = new QHBoxLayout();
    m_configureButton = new QPushButton("Configure & Connect", this);
    m_configureButton->setEnabled(false);
    
    m_refreshButton = new QPushButton("Refresh List", this);
    
    controlLayout->addWidget(m_refreshButton);
    controlLayout->addWidget(m_configureButton);
    mainLayout->addLayout(controlLayout);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: gray; font-style: italic;");
    mainLayout->addWidget(m_statusLabel);

    connect(m_configureButton, &QPushButton::clicked, this, [this]() {
        if (QListWidgetItem* item = m_interfaceListWidget->currentItem()) {
            onInterfaceSelected(item);
        }
    });
    connect(m_refreshButton, &QPushButton::clicked, this, &InterfaceWindow::refreshInterfaces);
    connect(m_interfaceListWidget, &QListWidget::itemSelectionChanged, this, [this]() {
        m_configureButton->setEnabled(m_interfaceListWidget->currentItem() != nullptr);
    });

    refreshInterfaces();
}

void InterfaceWindow::refreshInterfaces() {
    m_interfaceListWidget->clear();
    m_availableInterfaces = BusProtocol::queryInterfaces();

    for (const BusInterfaceInfo &info : m_availableInterfaces) {
        QString text = QString("[%1] %2 — %3").arg(info.type, info.name, info.description);
        QListWidgetItem* item = new QListWidgetItem(text, m_interfaceListWidget);
        item->setData(Qt::UserRole, info.name);
        item->setData(Qt::UserRole + 1, info.type);
    }
    m_statusLabel->setText(QString("Found %1 available interfaces.").arg(m_availableInterfaces.count()));
}

void InterfaceWindow::onInterfaceSelected(QListWidgetItem *item) {
    QString name = item->data(Qt::UserRole).toString();
    QString type = item->data(Qt::UserRole + 1).toString();

    ConfigDialog dialog(name, type, this);
    if (dialog.exec() == QDialog::Accepted) {
        QVariantMap config = dialog.getConfiguration();

        // Create main application window
        MainWindow* mainWin = new MainWindow(config);
        mainWin->show();

        // Close the setup window
        this->close();
    }
}

void InterfaceWindow::configureSelectedInterface() {
    if (QListWidgetItem* item = m_interfaceListWidget->currentItem()) {
        onInterfaceSelected(item);
    }
}
