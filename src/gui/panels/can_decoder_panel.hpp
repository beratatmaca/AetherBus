#pragma once

#include "core/can/dbc_parser.hpp"
#include "core/serial/serial_types.hpp"
#include <QGroupBox>
#include <QHash>
#include <QPair>
#include <QSet>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QTimer;

namespace aether {

class CanDecoderPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit CanDecoderPanel(QWidget *parent = nullptr);
    ~CanDecoderPanel() override;

    /** @brief Feed captured raw CAN chunks here to update the live signal table */
    void processChunk(const CapturedChunk &chunk);

private slots:
    void onLoadDbcClicked();
    void onAddCustomSignalClicked();
    void onSignalItemContextMenu(const QPoint &pos);
    void editSelectedSignal();
    void deleteSelectedSignal();

    /**
     * @brief Push dirty decoded values into the tree, at most once per timer tick —
     * per-frame setText at high bus loads is far more expensive than the
     * decode itself.
     */
    void flushDirtyValues();

private:
    /** @brief A decoded signal is identified by its message id plus signal name. */
    using SignalKey = QPair<quint32, QString>;

    void setupUi();
    void loadSettings();
    void saveSettings();
    void updateTable();
    void addOrUpdateSignalItem(const DbcMessage &msg, const DbcSignal &sig, bool isCustom);

    QTreeWidget *m_tree = nullptr;
    QPushButton *m_loadDbcBtn = nullptr;
    QPushButton *m_addCustomBtn = nullptr;
    QTimer *m_renderTimer = nullptr;

    DbcDatabase m_dbcDb;
    QString m_loadedDbcPath;

    QMap<quint32, QVector<DbcSignal>> m_customSignals;  ///< Custom signals added manually by the user
                                                        ///< Mapping: Message ID -> List of custom signals

    QHash<SignalKey, QString> m_lastValues;  ///< Last seen decoded value per signal, plus which ones changed since the
                                             ///< last render flush and a direct key -> tree item index so rendering
                                             ///< never has to scan the tree.
    QSet<SignalKey> m_dirtyValues;
    QHash<SignalKey, QTreeWidgetItem *> m_signalItems;
};

} // namespace aether
