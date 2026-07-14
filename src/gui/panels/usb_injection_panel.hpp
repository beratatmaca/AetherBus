#pragma once
#include <QGroupBox>

class QComboBox;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QLabel;

namespace aether {

/**
 * @brief USB packet injection panel to craft and transmit custom Control or Bulk transfers.
 */
class UsbInjectionPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit UsbInjectionPanel(QWidget *parent = nullptr);
    ~UsbInjectionPanel() override = default;

    void rescanDevices();

private slots:
    void onTransferTypeChanged(int index);
    void onInjectButtonClicked();

private:
    void buildUi();

    QComboBox *m_deviceBox = nullptr;
    QComboBox *m_typeBox = nullptr;
    QPushButton *m_injectBtn = nullptr;

    // Control fields
    QLineEdit *m_reqTypeEdit = nullptr;
    QLineEdit *m_reqEdit = nullptr;
    QLineEdit *m_valEdit = nullptr;
    QLineEdit *m_idxEdit = nullptr;
    QLineEdit *m_controlDataEdit = nullptr;

    // Bulk fields
    QLineEdit *m_epEdit = nullptr;
    QLineEdit *m_bulkDataEdit = nullptr;

    QWidget *m_controlContainer = nullptr;
    QWidget *m_bulkContainer = nullptr;
    QStackedWidget *m_stackedWidget = nullptr;
};

}  // namespace aether
