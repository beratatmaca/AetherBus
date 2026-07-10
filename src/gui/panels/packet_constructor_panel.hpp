#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QVector>

class QHBoxLayout;
class QLabel;

namespace aether {

class PacketConstructorPanel : public QWidget {
    Q_OBJECT

public:
    explicit PacketConstructorPanel(QWidget *parent = nullptr);
    ~PacketConstructorPanel() override = default;

    void resetPlaybackButton();

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
    void warnIfTcpUnsupported();

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

    QSpinBox *m_srcPortSpin = nullptr;
    QSpinBox *m_destPortSpin = nullptr;
    QLineEdit *m_payloadEdit = nullptr;
    QComboBox *m_payloadFormatBox = nullptr;

    QPushButton *m_sendBtn = nullptr;
    QCheckBox *m_periodicCheck = nullptr;
    QSpinBox *m_intervalSpin = nullptr;
    QTimer *m_timer = nullptr;

    QPushButton *m_playPcapBtn = nullptr;
    bool m_tcpWarningShown = false;

    /// A saved, ready-to-send raw packet, quick-sent with a single click.
    struct Macro {
        QString name;
        QByteArray rawPacket;
    };
    QVector<Macro> m_macros;
    QWidget *m_macroContainer = nullptr;
    QHBoxLayout *m_macroLayout = nullptr;
    QLabel *m_emptyMacroHint = nullptr;
};

}  // namespace aether
