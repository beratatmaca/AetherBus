#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QTimer;

namespace aether {

class InjectionPanel : public QWidget {
    Q_OBJECT

public:
    explicit InjectionPanel(QWidget *parent = nullptr);
    ~InjectionPanel() override;

    void setRunningState(bool running, bool directMode);

signals:
    void injectData(const QByteArray &data, bool toDevice);
    void injectionError(const QString &message);
    void fileSendRequested();

private slots:
    void sendInjection(bool toDevice);
    void onSendFileClicked();

private:
    QByteArray encodeInjection(bool &ok);

    QComboBox *m_injectFormatBox = nullptr;
    QLineEdit *m_injectEdit = nullptr;
    QComboBox *m_injectEndingBox = nullptr;
    QPushButton *m_toDeviceBtn = nullptr;
    QPushButton *m_toAppBtn = nullptr;
    QPushButton *m_fileBtn = nullptr;
    QCheckBox *m_repeatCheck = nullptr;
    QLineEdit *m_repeatIntervalEdit = nullptr;
    QTimer *m_repeatTimer = nullptr;
    bool m_repeatToDevice = true;
};

}  // namespace aether
