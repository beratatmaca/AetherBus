#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;

namespace aether {

class SignalPanel : public QWidget {
    Q_OBJECT

public:
    explicit SignalPanel(QWidget *parent = nullptr);
    ~SignalPanel() override;

    void updateModemStatus(bool cts, bool dsr, bool dcd, bool ri);
    bool isAutoReconnectEnabled() const;

signals:
    void rtsToggled(bool on);
    void dtrToggled(bool on);
    void breakTriggered();

private:
    QCheckBox *m_rtsCheck = nullptr;
    QCheckBox *m_dtrCheck = nullptr;
    QCheckBox *m_reconnectCheck = nullptr;
    QLabel *m_ctsLed = nullptr;
    QLabel *m_dsrLed = nullptr;
    QLabel *m_dcdLed = nullptr;
    QLabel *m_riLed = nullptr;
};

}  // namespace aether
