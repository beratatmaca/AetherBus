#pragma once
#include "core/usb/usb_types.hpp"
#include <QGroupBox>

class QComboBox;
class QPushButton;
class QLabel;

namespace aether {

/**
 * @brief USB session configuration panel.
 */
class UsbConfigPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit UsbConfigPanel(QWidget *parent = nullptr);
    ~UsbConfigPanel() override = default;

    [[nodiscard]] UsbConfig config() const;
    void setRunning(bool running);
    void setInterfaces(const QStringList &list);
    void setInterfaceName(const QString &name);
    void setStatus(const QString &htmlText);

signals:
    void startUsb(const aether::UsbConfig &cfg);
    void stopUsb();
    void rescanRequested();

private:
    void onStartButtonClicked();

    QComboBox *m_interfaceBox = nullptr;
    QPushButton *m_startButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    bool m_running = false;
};

}  // namespace aether
