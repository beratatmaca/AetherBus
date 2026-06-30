#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QStackedWidget>
#include <QFormLayout>
#include <QVariantMap>

class ConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfigDialog(const QString& interfaceName, const QString& interfaceType, QWidget *parent = nullptr);

    QVariantMap getConfiguration() const;

private slots:
    void onAccepted();

private:
    void setupCanLayout(QWidget* container);
    void setupSerialLayout(QWidget* container);
    void setupEthernetLayout(QWidget* container);
    void setupMockLayout(QWidget* container);

    QString m_interfaceName;
    QString m_interfaceType;
    QVariantMap m_configResult;

    // CAN settings
    QComboBox* m_canBaud;
    QComboBox* m_canFd;

    // Serial settings
    QComboBox* m_serialBaud;
    QComboBox* m_serialDataBits;
    QComboBox* m_serialParity;
    QComboBox* m_serialStopBits;

    // Ethernet settings
    QLineEdit* m_ethDestIp;
    QLineEdit* m_ethPort;
    QComboBox* m_ethProto;
};
