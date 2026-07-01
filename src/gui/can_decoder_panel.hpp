#pragma once

#include "core/can/dbc_parser.hpp"
#include "core/serial/serial_types.hpp"
#include <QGroupBox>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;

namespace aether {

class CanDecoderPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit CanDecoderPanel(QWidget *parent = nullptr);
    ~CanDecoderPanel() override;

    /// Feed captured raw CAN chunks here to update the live signal table
    void processChunk(const CapturedChunk &chunk);

private slots:
    void onLoadDbcClicked();
    void onAddCustomSignalClicked();
    void onSignalItemContextMenu(const QPoint &pos);
    void editSelectedSignal();
    void deleteSelectedSignal();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    void updateTable();
    void addOrUpdateSignalItem(const DbcMessage &msg, const DbcSignal &sig, bool isCustom);

    QTreeWidget *m_tree = nullptr;
    QPushButton *m_loadDbcBtn = nullptr;
    QPushButton *m_addCustomBtn = nullptr;

    DbcDatabase m_dbcDb;
    QString m_loadedDbcPath;

    // Custom signals added manually by the user
    // Mapping: Message ID -> List of custom signals
    QMap<quint32, QVector<DbcSignal>> m_customSignals;

    // Last seen signal raw/scaled values for display caching
    // Mapping: "MsgID_SignalName" -> display string
    QMap<QString, QString> m_lastValues;
};

} // namespace aether
