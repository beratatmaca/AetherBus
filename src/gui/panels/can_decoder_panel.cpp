#include "gui/panels/can_decoder_panel.hpp"
#include <QDialog>
#include <QFormLayout>
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QTreeWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMenu>

namespace aether {

// Inline Dialog to Add/Edit custom signals
class SignalEditDialog : public QDialog {
public:
    explicit SignalEditDialog(QWidget *parent = nullptr, const DbcSignal *existingSig = nullptr, quint32 existingId = 0,
                              const QString &existingMsgName = QString())
        : QDialog(parent) {
        setWindowTitle(existingSig ? QStringLiteral("Edit Signal Decoder") : QStringLiteral("Add Custom Signal Decoder"));
        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout();

        m_idEdit = new QLineEdit(this);
        m_idEdit->setPlaceholderText(QStringLiteral("e.g. 1A4"));
        m_idEdit->setToolTip(QStringLiteral("CAN Frame ID in hexadecimal"));
        if (existingId > 0)
            m_idEdit->setText(QString::number(existingId, 16).toUpper());
        form->addRow(QStringLiteral("CAN ID (Hex):"), m_idEdit);

        m_msgEdit = new QLineEdit(this);
        m_msgEdit->setPlaceholderText(QStringLiteral("e.g. EngineStatus"));
        if (!existingMsgName.isEmpty())
            m_msgEdit->setText(existingMsgName);
        form->addRow(QStringLiteral("Message Name:"), m_msgEdit);

        m_nameEdit = new QLineEdit(this);
        m_nameEdit->setPlaceholderText(QStringLiteral("e.g. EngineRPM"));
        if (existingSig)
            m_nameEdit->setText(existingSig->name);
        form->addRow(QStringLiteral("Signal Name:"), m_nameEdit);

        m_startEdit = new QLineEdit(QStringLiteral("0"), this);
        if (existingSig)
            m_startEdit->setText(QString::number(existingSig->startBit));
        form->addRow(QStringLiteral("Start Bit (0-511):"), m_startEdit);

        m_lengthEdit = new QLineEdit(QStringLiteral("8"), this);
        if (existingSig)
            m_lengthEdit->setText(QString::number(existingSig->bitLength));
        form->addRow(QStringLiteral("Bit Length (1-64):"), m_lengthEdit);

        m_endianBox = new QComboBox(this);
        m_endianBox->addItem(QStringLiteral("Intel (Little Endian)"), 0);
        m_endianBox->addItem(QStringLiteral("Motorola (Big Endian)"), 1);
        if (existingSig)
            m_endianBox->setCurrentIndex(existingSig->isBigEndian ? 1 : 0);
        form->addRow(QStringLiteral("Byte Order:"), m_endianBox);

        m_signedCheck = new QCheckBox(QStringLiteral("Signed Value"), this);
        if (existingSig)
            m_signedCheck->setChecked(existingSig->isSigned);
        form->addRow(QStringLiteral("Type:"), m_signedCheck);

        m_factorEdit = new QLineEdit(QStringLiteral("1.0"), this);
        if (existingSig)
            m_factorEdit->setText(QString::number(existingSig->factor));
        form->addRow(QStringLiteral("Scaling Factor:"), m_factorEdit);

        m_offsetEdit = new QLineEdit(QStringLiteral("0.0"), this);
        if (existingSig)
            m_offsetEdit->setText(QString::number(existingSig->offset));
        form->addRow(QStringLiteral("Scaling Offset:"), m_offsetEdit);

        m_unitEdit = new QLineEdit(this);
        m_unitEdit->setPlaceholderText(QStringLiteral("e.g. rpm, C, V"));
        if (existingSig)
            m_unitEdit->setText(existingSig->unit);
        form->addRow(QStringLiteral("Unit:"), m_unitEdit);

        layout->addLayout(form);

        auto *buttons = new QHBoxLayout();
        auto *okBtn = new QPushButton(QStringLiteral("Save"), this);
        connect(okBtn, &QPushButton::clicked, this, &SignalEditDialog::accept);
        auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
        connect(cancelBtn, &QPushButton::clicked, this, &SignalEditDialog::reject);
        buttons->addWidget(okBtn);
        buttons->addWidget(cancelBtn);
        layout->addLayout(buttons);
    }

    quint32 canId() const {
        bool ok;
        return m_idEdit->text().trimmed().toUInt(&ok, 16);
    }
    QString messageName() const { return m_msgEdit->text().trimmed(); }
    DbcSignal getSignal() const {
        DbcSignal sig;
        sig.name = m_nameEdit->text().trimmed();
        sig.startBit = m_startEdit->text().toInt();
        sig.bitLength = m_lengthEdit->text().toInt();
        sig.isBigEndian = (m_endianBox->currentIndex() == 1);
        sig.isSigned = m_signedCheck->isChecked();
        sig.factor = m_factorEdit->text().toDouble();
        sig.offset = m_offsetEdit->text().toDouble();
        sig.unit = m_unitEdit->text().trimmed();
        return sig;
    }

private:
    QLineEdit *m_idEdit = nullptr;
    QLineEdit *m_msgEdit = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_startEdit = nullptr;
    QLineEdit *m_lengthEdit = nullptr;
    QComboBox *m_endianBox = nullptr;
    QCheckBox *m_signedCheck = nullptr;
    QLineEdit *m_factorEdit = nullptr;
    QLineEdit *m_offsetEdit = nullptr;
    QLineEdit *m_unitEdit = nullptr;
};

CanDecoderPanel::CanDecoderPanel(QWidget *parent) : QGroupBox(QStringLiteral("CAN DBC Decoder"), parent) {
    setupUi();
    loadSettings();
}

CanDecoderPanel::~CanDecoderPanel() {
    saveSettings();
}

void CanDecoderPanel::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    auto *btnRow = new QHBoxLayout();
    m_loadDbcBtn = new QPushButton(QStringLiteral("Load DBC…"), this);
    m_loadDbcBtn->setToolTip(QStringLiteral("Import messages and signals from a standard .dbc file"));
    connect(m_loadDbcBtn, &QPushButton::clicked, this, &CanDecoderPanel::onLoadDbcClicked);

    m_addCustomBtn = new QPushButton(QStringLiteral("+ Custom Signal"), this);
    m_addCustomBtn->setToolTip(QStringLiteral("Manually define a custom signal conversion rule"));
    connect(m_addCustomBtn, &QPushButton::clicked, this, &CanDecoderPanel::onAddCustomSignalClicked);

    btnRow->addWidget(m_loadDbcBtn);
    btnRow->addWidget(m_addCustomBtn);
    layout->addLayout(btnRow);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(6);
    m_tree->setHeaderLabels({QStringLiteral("Signal / Message"), QStringLiteral("Live Value"), QStringLiteral("Unit"),
                             QStringLiteral("Type"), QStringLiteral("Start"), QStringLiteral("Length")});
    m_tree->header()->setStretchLastSection(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &CanDecoderPanel::onSignalItemContextMenu);

    layout->addWidget(m_tree);
}

void CanDecoderPanel::updateTable() {
    m_tree->clear();

    // 1. Add DBC Database Messages
    for (const auto &msg : m_dbcDb.messages()) {
        auto *msgItem = new QTreeWidgetItem(m_tree);
        msgItem->setText(0, QStringLiteral("%1 (0x%2)").arg(msg.name).arg(QString::number(msg.id, 16).toUpper()));
        msgItem->setData(0, Qt::UserRole, msg.id);
        msgItem->setData(0, Qt::UserRole + 1, false);  // isCustom = false

        for (const auto &sig : msg.signalList) {
            addOrUpdateSignalItem(msg, sig, false);
        }
    }

    // 2. Add Custom Signals
    for (auto it = m_customSignals.begin(); it != m_customSignals.end(); ++it) {
        quint32 id = it.key();
        DbcMessage mockMsg;
        mockMsg.id = id;
        mockMsg.name = QStringLiteral("Custom_0x%1").arg(QString::number(id, 16).toUpper());

        auto *msgItem = new QTreeWidgetItem(m_tree);
        msgItem->setText(0, QStringLiteral("%1 (0x%2)").arg(mockMsg.name).arg(QString::number(id, 16).toUpper()));
        msgItem->setData(0, Qt::UserRole, id);
        msgItem->setData(0, Qt::UserRole + 1, true);  // isCustom = true

        for (const auto &sig : it.value()) {
            addOrUpdateSignalItem(mockMsg, sig, true);
        }
    }

    m_tree->expandAll();
}

void CanDecoderPanel::addOrUpdateSignalItem(const DbcMessage &msg, const DbcSignal &sig, bool isCustom) {
    // Find parent message item in tree
    QTreeWidgetItem *parentItem = nullptr;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *item = m_tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toUInt() == msg.id && item->data(0, Qt::UserRole + 1).toBool() == isCustom) {
            parentItem = item;
            break;
        }
    }

    if (!parentItem)
        return;

    auto *sigItem = new QTreeWidgetItem(parentItem);
    sigItem->setText(0, sig.name);

    // Check if we have a last value cached
    QString cacheKey = QStringLiteral("%1_%2").arg(msg.id).arg(sig.name);
    sigItem->setText(1, m_lastValues.value(cacheKey, QStringLiteral("---")));
    sigItem->setText(2, sig.unit);
    sigItem->setText(3, sig.isSigned ? QStringLiteral("Signed") : QStringLiteral("Unsigned"));
    sigItem->setText(4, QString::number(sig.startBit));
    sigItem->setText(5, QString::number(sig.bitLength));

    // Store metadata on child node
    sigItem->setData(0, Qt::UserRole, msg.id);
    sigItem->setData(0, Qt::UserRole + 1, isCustom);
    sigItem->setData(0, Qt::UserRole + 2, sig.name);  // signal name
}

void CanDecoderPanel::processChunk(const CapturedChunk &chunk) {
    if (!chunk.isFrame)
        return;

    quint32 id = chunk.frameId;

    // Check custom signals
    if (m_customSignals.contains(id)) {
        for (const auto &sig : m_customSignals[id]) {
            double val = DbcDatabase::decodeSignal(chunk.data, sig);
            QString cacheKey = QStringLiteral("%1_%2").arg(id).arg(sig.name);
            m_lastValues[cacheKey] = QString::number(val, 'f', 2);
        }
    }

    // Check DBC signals
    if (m_dbcDb.contains(id)) {
        const auto &msg = m_dbcDb.getMessage(id);
        for (const auto &sig : msg.signalList) {
            double val = DbcDatabase::decodeSignal(chunk.data, sig);
            QString cacheKey = QStringLiteral("%1_%2").arg(id).arg(sig.name);
            m_lastValues[cacheKey] = QString::number(val, 'f', 2);
        }
    }

    // Update tree values inline if displayed
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *msgItem = m_tree->topLevelItem(i);
        if (msgItem->data(0, Qt::UserRole).toUInt() == id) {
            for (int j = 0; j < msgItem->childCount(); ++j) {
                auto *child = msgItem->child(j);
                QString sigName = child->data(0, Qt::UserRole + 2).toString();
                QString cacheKey = QStringLiteral("%1_%2").arg(id).arg(sigName);
                if (m_lastValues.contains(cacheKey)) {
                    child->setText(1, m_lastValues.value(cacheKey));
                }
            }
        }
    }
}

void CanDecoderPanel::onLoadDbcClicked() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open CAN DBC File"), QString(),
                                                      QStringLiteral("DBC database files (*.dbc);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Could not open DBC file: %1").arg(path));
        return;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    if (m_dbcDb.parse(content)) {
        m_loadedDbcPath = path;
        updateTable();
    } else {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Failed to parse DBC file or file is empty."));
    }
}

void CanDecoderPanel::onAddCustomSignalClicked() {
    SignalEditDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        quint32 id = dlg.canId();
        DbcSignal sig = dlg.getSignal();

        m_customSignals[id].append(sig);
        updateTable();
        saveSettings();
    }
}

void CanDecoderPanel::onSignalItemContextMenu(const QPoint &pos) {
    auto *item = m_tree->itemAt(pos);
    if (!item)
        return;

    // We can only edit/delete custom signal nodes
    bool isCustom = item->data(0, Qt::UserRole + 1).toBool();
    QString sigName = item->data(0, Qt::UserRole + 2).toString();

    if (!isCustom || sigName.isEmpty())
        return;  // Message root or DBC signal cannot be edited

    QMenu menu(this);
    auto *editAct = menu.addAction(QStringLiteral("Edit Signal Decoder…"));
    auto *delAct = menu.addAction(QStringLiteral("Delete Signal Decoder"));

    connect(editAct, &QAction::triggered, this, &CanDecoderPanel::editSelectedSignal);
    connect(delAct, &QAction::triggered, this, &CanDecoderPanel::deleteSelectedSignal);

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void CanDecoderPanel::editSelectedSignal() {
    auto *item = m_tree->currentItem();
    if (!item)
        return;

    quint32 id = item->data(0, Qt::UserRole).toUInt();
    QString sigName = item->data(0, Qt::UserRole + 2).toString();

    if (!m_customSignals.contains(id))
        return;

    // Find the signal
    auto &sigs = m_customSignals[id];
    for (int i = 0; i < sigs.size(); ++i) {
        if (sigs.at(i).name == sigName) {
            SignalEditDialog dlg(this, &sigs.at(i), id, item->parent()->text(0).split(QLatin1Char(' ')).first());
            if (dlg.exec() == QDialog::Accepted) {
                // Delete old cache
                QString cacheKey = QStringLiteral("%1_%2").arg(id).arg(sigName);
                m_lastValues.remove(cacheKey);

                // Update signal
                quint32 newId = dlg.canId();
                DbcSignal newSig = dlg.getSignal();

                sigs.removeAt(i);
                if (sigs.isEmpty()) {
                    m_customSignals.remove(id);
                }

                m_customSignals[newId].append(newSig);
                updateTable();
                saveSettings();
            }
            break;
        }
    }
}

void CanDecoderPanel::deleteSelectedSignal() {
    auto *item = m_tree->currentItem();
    if (!item)
        return;

    quint32 id = item->data(0, Qt::UserRole).toUInt();
    QString sigName = item->data(0, Qt::UserRole + 2).toString();

    if (m_customSignals.contains(id)) {
        auto &sigs = m_customSignals[id];
        for (int i = 0; i < sigs.size(); ++i) {
            if (sigs.at(i).name == sigName) {
                sigs.removeAt(i);
                QString cacheKey = QStringLiteral("%1_%2").arg(id).arg(sigName);
                m_lastValues.remove(cacheKey);
                break;
            }
        }
        if (sigs.isEmpty()) {
            m_customSignals.remove(id);
        }
        updateTable();
        saveSettings();
    }
}

void CanDecoderPanel::loadSettings() {
    QSettings settings;

    // Restore DBC file path
    m_loadedDbcPath = settings.value(QStringLiteral("can/dbc_path")).toString();
    if (!m_loadedDbcPath.isEmpty()) {
        QFile file(m_loadedDbcPath);
        if (file.open(QFile::ReadOnly | QFile::Text)) {
            m_dbcDb.parse(QString::fromUtf8(file.readAll()));
        }
    }

    // Restore Custom signals
    m_customSignals.clear();
    int size = settings.beginReadArray(QStringLiteral("can/custom_signals"));
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        quint32 id = settings.value(QStringLiteral("id")).toUInt();

        DbcSignal sig;
        sig.name = settings.value(QStringLiteral("name")).toString();
        sig.startBit = settings.value(QStringLiteral("start_bit")).toInt();
        sig.bitLength = settings.value(QStringLiteral("bit_length")).toInt();
        sig.isBigEndian = settings.value(QStringLiteral("is_big_endian")).toBool();
        sig.isSigned = settings.value(QStringLiteral("is_signed")).toBool();
        sig.factor = settings.value(QStringLiteral("factor"), 1.0).toDouble();
        sig.offset = settings.value(QStringLiteral("offset"), 0.0).toDouble();
        sig.unit = settings.value(QStringLiteral("unit")).toString();

        m_customSignals[id].append(sig);
    }
    settings.endArray();

    updateTable();
}

void CanDecoderPanel::saveSettings() {
    QSettings settings;
    settings.setValue(QStringLiteral("can/dbc_path"), m_loadedDbcPath);

    // Save Custom signals
    settings.beginWriteArray(QStringLiteral("can/custom_signals"));
    int index = 0;
    for (auto it = m_customSignals.begin(); it != m_customSignals.end(); ++it) {
        quint32 id = it.key();
        for (const auto &sig : it.value()) {
            settings.setArrayIndex(index++);
            settings.setValue(QStringLiteral("id"), id);
            settings.setValue(QStringLiteral("name"), sig.name);
            settings.setValue(QStringLiteral("start_bit"), sig.startBit);
            settings.setValue(QStringLiteral("bit_length"), sig.bitLength);
            settings.setValue(QStringLiteral("is_big_endian"), sig.isBigEndian);
            settings.setValue(QStringLiteral("is_signed"), sig.isSigned);
            settings.setValue(QStringLiteral("factor"), sig.factor);
            settings.setValue(QStringLiteral("offset"), sig.offset);
            settings.setValue(QStringLiteral("unit"), sig.unit);
        }
    }
    settings.endArray();
}

}  // namespace aether
