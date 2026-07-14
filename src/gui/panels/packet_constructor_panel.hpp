#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QVector>
#include "gui/widgets/macro_button_bar.hpp"

class QHBoxLayout;
class QLabel;

namespace aether {

class PacketConstructorPanel : public QWidget {
    Q_OBJECT

public:
    explicit PacketConstructorPanel(QWidget *parent = nullptr);
    ~PacketConstructorPanel() override = default;

    void resetPlaybackButton();

    /**
     * @brief Fire the quick-send macro at @p index (emits @ref packetReady), for the
     * control channel's `run_macro` verb.
     * @return false if @p index is out of range.
     */
    bool triggerMacro(int index);

    /** @brief Index of the first quick-send macro named @p name, or -1 if none. */
    [[nodiscard]] int indexOfMacro(const QString &name) const;

signals:
    void packetReady(const QByteArray &packet);
    void playPcapRequested(const QString &filePath);
    void stopPlaybackRequested();

private slots:
    void onSendClicked();
    void onPeriodicToggled(bool checked);
    void onSendPeriodic();
    void onLoadPcapClicked();

private:
    void buildUi();
    QByteArray buildPacket(bool *ok = nullptr);
    void updateProtocolFieldsVisibility();

    void loadMacros();
    void saveMacros();
    void rebuildMacroButtons();
    void saveCurrentAsMacro();

    // GUI controls
    QLineEdit *m_srcMacEdit = nullptr;
    QLineEdit *m_destMacEdit = nullptr;
    QComboBox *m_etherTypeBox = nullptr;

    QLineEdit *m_srcIpEdit = nullptr;
    QLineEdit *m_destIpEdit = nullptr;
    QSpinBox *m_ttlSpin = nullptr;
    QComboBox *m_ipProtoBox = nullptr;

    QLabel *m_srcPortLabel = nullptr;
    QSpinBox *m_srcPortSpin = nullptr;
    QLabel *m_destPortLabel = nullptr;
    QSpinBox *m_destPortSpin = nullptr;

    QLabel *m_tcpSeqLabel = nullptr;
    QLineEdit *m_tcpSeqEdit = nullptr;
    QLabel *m_tcpAckLabel = nullptr;
    QLineEdit *m_tcpAckEdit = nullptr;
    QLabel *m_tcpWindowLabel = nullptr;
    QSpinBox *m_tcpWindowSpin = nullptr;
    QLabel *m_tcpFlagsLabel = nullptr;
    QWidget *m_tcpFlagsWidget = nullptr;
    QCheckBox *m_synCheck = nullptr;
    QCheckBox *m_ackCheck = nullptr;
    QCheckBox *m_finCheck = nullptr;
    QCheckBox *m_rstCheck = nullptr;
    QCheckBox *m_pshCheck = nullptr;
    QCheckBox *m_urgCheck = nullptr;

    QLineEdit *m_payloadEdit = nullptr;
    QComboBox *m_payloadFormatBox = nullptr;

    QPushButton *m_sendBtn = nullptr;
    QCheckBox *m_periodicCheck = nullptr;
    QSpinBox *m_intervalSpin = nullptr;
    QTimer *m_timer = nullptr;

    QPushButton *m_playPcapBtn = nullptr;

    /** @brief A saved, ready-to-send raw packet, quick-sent with a single click. */
    struct EthernetMacro {
        QString name;
        QByteArray rawPacket;
    };

    class EthernetMacroBar : public MacroButtonBar {
    public:
        explicit EthernetMacroBar(PacketConstructorPanel *parent);
        void load();
        void save();
        void addMacro(const EthernetMacro &macro);
    protected:
        [[nodiscard]] int macroCount() const override;
        [[nodiscard]] QString macroName(int index) const override;
        [[nodiscard]] QString macroToolTip(int index) const override;
        void onMacroTriggered(int index) override;
        void buildContextMenu(int index, QMenu &menu) override;
    private:
        QVector<EthernetMacro> m_macros;
        PacketConstructorPanel *m_panel;
    };

    EthernetMacroBar *m_macroBar = nullptr;
};

}  // namespace aether
