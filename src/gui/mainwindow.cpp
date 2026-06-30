#include "mainwindow.h"
#include "core/bus_protocol.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QToolBar>
#include <QStatusBar>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QTimer>
#include <QDateTime>

MainWindow::MainWindow(const QVariantMap& config, QWidget *parent)
    : QMainWindow(parent), m_config(config), m_capturing(false) {
    
    setWindowTitle(QString("AetherBus Console [%1]").arg(config["interface"].toString()));
    resize(1024, 768);

    setupUI();

    m_simulator = new SimulatorWorker(this);
    connect(m_simulator, &SimulatorWorker::frameReady, m_tableModel, &FrameTableModel::addFrame);
}

void MainWindow::setupUI() {
    // 1. Toolbar Configuration
    QToolBar* toolbar = addToolBar("Control Toolbar");
    toolbar->setMovable(false);

    m_captureButton = new QPushButton("▶ Start Capture", this);
    m_captureButton->setStyleSheet("font-weight: bold; background-color: #2e7d32; color: white; padding: 4px 10px;");
    connect(m_captureButton, &QPushButton::clicked, this, &MainWindow::toggleCapture);
    toolbar->addWidget(m_captureButton);

    toolbar->addSeparator();

    QPushButton* clearButton = new QPushButton("🗑 Clear Log", this);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearTrace);
    toolbar->addWidget(clearButton);

    toolbar->addSeparator();

    toolbar->addWidget(new QLabel("  Filter:  ", this));
    m_filterBar = new QLineEdit(this);
    m_filterBar->setPlaceholderText("e.g. id == 0x1A0 || interface == Bus 0");
    m_filterBar->setMaximumWidth(300);
    toolbar->addWidget(m_filterBar);

    // 2. Setup Central Workspace Splitter
    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(mainSplitter);

    // Left Workspace (Trace & Bytes)
    QSplitter* leftSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    mainSplitter->addWidget(leftSplitter);

    // Zone 1: Trace View Table
    QWidget* zone1Container = new QWidget(leftSplitter);
    QVBoxLayout* zone1Layout = new QVBoxLayout(zone1Container);
    zone1Layout->setContentsMargins(0, 0, 0, 0);

    QLabel* traceLabel = new QLabel("Zone 1: Frame Traffic Trace Log", zone1Container);
    traceLabel->setStyleSheet("font-weight: bold; padding: 3px; background-color: #f5f5f5;");
    zone1Layout->addWidget(traceLabel);

    m_traceTableView = new QTableView(zone1Container);
    m_tableModel = new FrameTableModel(this);
    m_traceTableView->setModel(m_tableModel);
    m_traceTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_traceTableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_traceTableView->horizontalHeader()->setStretchLastSection(true);
    m_traceTableView->verticalHeader()->setVisible(false);
    m_traceTableView->horizontalHeader()->setDefaultSectionSize(80);
    zone1Layout->addWidget(m_traceTableView);
    leftSplitter->addWidget(zone1Container);

    connect(m_traceTableView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::onFrameSelected);

    // Zone 2: HTerm Bytes Grid View
    QWidget* zone2Container = new QWidget(leftSplitter);
    QVBoxLayout* zone2Layout = new QVBoxLayout(zone2Container);
    zone2Layout->setContentsMargins(0, 0, 0, 0);

    QLabel* htermLabel = new QLabel("Zone 2: HTerm Synchronized Bytes Inspector", zone2Container);
    htermLabel->setStyleSheet("font-weight: bold; padding: 3px; background-color: #f5f5f5;");
    zone2Layout->addWidget(htermLabel);

    QWidget* bytesWidget = new QWidget(zone2Container);
    QHBoxLayout* bytesLayout = new QHBoxLayout(bytesWidget);
    bytesLayout->setContentsMargins(0, 0, 0, 0);

    auto createHtermColumn = [this, bytesWidget](const QString& header, QTextEdit*& editor) {
        QWidget* col = new QWidget(bytesWidget);
        QVBoxLayout* l = new QVBoxLayout(col);
        l->setContentsMargins(2, 2, 2, 2);
        l->addWidget(new QLabel(header, col));
        editor = new QTextEdit(col);
        editor->setReadOnly(true);
        editor->setFontFamily("Monospace");
        editor->setFontPointSize(10);
        l->addWidget(editor);
        return col;
    };

    bytesLayout->addWidget(createHtermColumn("ASCII", m_htermAscii));
    bytesLayout->addWidget(createHtermColumn("HEX", m_htermHex));
    bytesLayout->addWidget(createHtermColumn("BINARY", m_htermBin));
    bytesLayout->addWidget(createHtermColumn("DECIMAL", m_htermDec));
    
    zone2Layout->addWidget(bytesWidget);
    leftSplitter->addWidget(zone2Container);

    // Right Workspace (Details & Injection)
    QSplitter* rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    mainSplitter->addWidget(rightSplitter);

    // Zone 3: Details Tree
    QWidget* zone3Container = new QWidget(rightSplitter);
    QVBoxLayout* zone3Layout = new QVBoxLayout(zone3Container);
    zone3Layout->setContentsMargins(0, 0, 0, 0);

    QLabel* detailsLabel = new QLabel("Zone 3: Signal Tree Decoder Explorer", zone3Container);
    detailsLabel->setStyleSheet("font-weight: bold; padding: 3px; background-color: #f5f5f5;");
    zone3Layout->addWidget(detailsLabel);

    m_detailsTreeView = new QTreeView(zone3Container);
    m_treeModel = new QStandardItemModel(this);
    m_treeModel->setHorizontalHeaderLabels({"Parameter/Signal", "Value", "Unit"});
    m_detailsTreeView->setModel(m_treeModel);
    m_detailsTreeView->header()->setStretchLastSection(true);
    zone3Layout->addWidget(m_detailsTreeView);
    rightSplitter->addWidget(zone3Container);

    // Zone 4: Injection Dashboard
    QGroupBox* zone4Container = new QGroupBox("Zone 4: Message Injection Dashboard", rightSplitter);
    QFormLayout* injectFormLayout = new QFormLayout(zone4Container);

    m_injectIdInput = new QLineEdit("1F0", zone4Container);
    m_injectDataInput = new QLineEdit("11 22 33 AA BB CC DD EE", zone4Container);
    m_injectButton = new QPushButton("⚡ Inject Payload Once", zone4Container);
    m_injectButton->setStyleSheet("font-weight: bold; background-color: #0d47a1; color: white; padding: 5px;");

    injectFormLayout->addRow("Outbound ID (Hex):", m_injectIdInput);
    injectFormLayout->addRow("Data Bytes (Hex):", m_injectDataInput);
    injectFormLayout->addRow(m_injectButton);

    connect(m_injectButton, &QPushButton::clicked, this, &MainWindow::injectSingleMessage);
    rightSplitter->addWidget(zone4Container);

    // Setup Splitter sizes (equal heights initially)
    leftSplitter->setSizes({400, 200});
    rightSplitter->setSizes({400, 200});
    mainSplitter->setSizes({650, 350});

    // 3. Status Bar setup
    m_statusLabel = new QLabel(QString("Connected to %1 (%2) — Ready.").arg(m_config["interface"].toString(), m_config["type"].toString()), this);
    statusBar()->addWidget(m_statusLabel);
}

void MainWindow::toggleCapture() {
    m_capturing = !m_capturing;
    if (m_capturing) {
        m_captureButton->setText("■ Stop Capture");
        m_captureButton->setStyleSheet("font-weight: bold; background-color: #b71c1c; color: white; padding: 4px 10px;");
        m_statusLabel->setText(QString("Capturing traffic on %1...").arg(m_config["interface"].toString()));
        m_simulator->start();
    } else {
        m_captureButton->setText("▶ Start Capture");
        m_captureButton->setStyleSheet("font-weight: bold; background-color: #2e7d32; color: white; padding: 4px 10px;");
        m_statusLabel->setText("Capture stopped.");
        m_simulator->stop();
    }
}

void MainWindow::onFrameSelected(const QModelIndex& index) {
    if (!index.isValid()) return;
    AetherFrame frame = m_tableModel->getFrame(index.row());
    updateHTermView(frame);
    updateDetailsTree(frame);
}

void MainWindow::updateHTermView(const AetherFrame& frame) {
    m_htermAscii->clear();
    m_htermHex->clear();
    m_htermBin->clear();
    m_htermDec->clear();

    for (uint8_t byte : frame.data) {
        // ASCII Representation (control chars replaced with dots or indicators)
        if (byte >= 32 && byte <= 126) {
            m_htermAscii->append(QString(QChar(byte)));
        } else if (byte == 0x0D) {
            m_htermAscii->append("[CR]");
        } else if (byte == 0x0A) {
            m_htermAscii->append("[LF]");
        } else {
            m_htermAscii->append(".");
        }

        // HEX
        m_htermHex->append(QString("%1").arg(byte, 2, 16, QChar('0')).toUpper());

        // BIN
        m_htermBin->append(QString("%1").arg(byte, 8, 2, QChar('0')));

        // DEC
        m_htermDec->append(QString::number(byte));
    }
}

void MainWindow::updateDetailsTree(const AetherFrame& frame) {
    m_treeModel->removeRows(0, m_treeModel->rowCount());

    QStandardItem* rootItem = m_treeModel->invisibleRootItem();

    QStandardItem* idItem = new QStandardItem(QString("Message ID: 0x%1").arg(frame.payload_id, 0, 16).toUpper());
    QStandardItem* valItem = new QStandardItem(QString("Bus interface %1").arg(frame.bus_identifier));
    rootItem->appendRow({idItem, valItem});

    // Real binary decoding based on frame IDs
    if (frame.payload_id == 0x1A0) {
        if (frame.data.size() >= 2) {
            // Unpack Big-Endian uint16 Engine RPM
            uint16_t rpm = (static_cast<uint16_t>(frame.data[0]) << 8) | frame.data[1];
            
            QStandardItem* signal = new QStandardItem("Engine_RPM");
            QStandardItem* value = new QStandardItem(QString::number(rpm));
            QStandardItem* unit = new QStandardItem("RPM");
            idItem->appendRow({signal, value, unit});
        }
    } else if (frame.payload_id == 0x1F0) {
        if (frame.data.size() >= 1) {
            // Unpack uint8 temperature
            uint8_t temp = frame.data[0];
            
            QStandardItem* signal = new QStandardItem("Coolant_Temp");
            QStandardItem* value = new QStandardItem(QString::number(temp));
            QStandardItem* unit = new QStandardItem("°C");
            idItem->appendRow({signal, value, unit});
        }
    } else {
        // Generic Raw Bytes listing
        for (int i = 0; i < frame.data.size(); ++i) {
            QStandardItem* byteSignal = new QStandardItem(QString("Byte %1").arg(i));
            QStandardItem* byteValue = new QStandardItem(QString("0x%1").arg(frame.data[i], 2, 16, QChar('0')).toUpper());
            QStandardItem* byteUnit = new QStandardItem("Raw");
            idItem->appendRow({byteSignal, byteValue, byteUnit});
        }
    }
    m_detailsTreeView->expandAll();
}

void MainWindow::injectSingleMessage() {
    bool ok;
    uint32_t id = m_injectIdInput->text().toUInt(&ok, 16);
    if (!ok) {
        QMessageBox::warning(this, "Injection Error", "Please provide a valid Hexadecimal message ID.");
        return;
    }

    QString dataStr = m_injectDataInput->text().simplified();
    QStringList tokens = dataStr.split(' ');
    std::vector<uint8_t> data;
    for (const QString& t : tokens) {
        if (t.isEmpty()) continue;
        uint8_t byte = t.toUInt(&ok, 16);
        if (!ok) {
            QMessageBox::warning(this, "Injection Error", QString("Invalid hex byte: %1").arg(t));
            return;
        }
        data.push_back(byte);
    }

    AetherFrame injectedFrame;
    injectedFrame.timestamp_us = QDateTime::currentMSecsSinceEpoch() * 1000;
    injectedFrame.bus_identifier = 0; // Simulated outbound bus
    injectedFrame.payload_id = id;
    injectedFrame.length = data.size();
    injectedFrame.data = data;

    // Immediately push back into the trace logs to visualize loopback
    m_tableModel->addFrame(injectedFrame);
    m_statusLabel->setText(QString("Injected frame 0x%1 successfully.").arg(id, 0, 16).toUpper());
}

void MainWindow::clearTrace() {
    m_tableModel->clear();
    m_treeModel->removeRows(0, m_treeModel->rowCount());
    m_htermAscii->clear();
    m_htermHex->clear();
    m_htermBin->clear();
    m_htermDec->clear();
    m_statusLabel->setText("Logs cleared.");
}
