#pragma once

#include "core/serial/serial_types.hpp"
#include <QGroupBox>

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;

namespace aether {

class ConfigPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit ConfigPanel(QWidget *parent = nullptr);
    ~ConfigPanel() override;

    void setRunningState(bool running);
    void setStatus(const QString &htmlText);
    void populateDevices(const QStringList &systemPorts, const QStringList &byIdPorts);
    QString device() const;

signals:
    void startInterception(const SerialConfig &cfg);
    void stopInterception();
    void rescanRequested();
    /// Emitted whenever the status text changes, as plain text plus an error flag,
    /// so the host window can mirror it into the always-visible status bar.
    void statusChanged(const QString &plainText, bool isError);

private slots:
    void onStartButtonClicked();

private:
    QComboBox *m_deviceBox = nullptr;
    QComboBox *m_baudBox = nullptr;
    QComboBox *m_dataBitsBox = nullptr;
    QComboBox *m_parityBox = nullptr;
    QComboBox *m_stopBitsBox = nullptr;
    QComboBox *m_flowBox = nullptr;
    QLineEdit *m_symlinkEdit = nullptr;
    QCheckBox *m_directCheck = nullptr;
    QPushButton *m_startButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    bool m_isRunning = false;
};

}  // namespace aether
