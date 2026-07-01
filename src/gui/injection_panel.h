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
    /**
     * @brief Request that the current input be saved as a reusable macro.
     * @param format   Input format index (0=HEX, 1=ASCII, 2=DEC, 3=BIN).
     * @param payload  Raw payload text in that format.
     * @param ending   Line-ending index (0=none, 1=CR, 2=LF, 3=CR+LF).
     * @param toDevice Last-used send direction (@c true device, @c false app).
     */
    void saveAsMacroRequested(int format, const QString &payload, int ending, bool toDevice);

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
    QPushButton *m_saveMacroBtn = nullptr;
    QCheckBox *m_repeatCheck = nullptr;
    QLineEdit *m_repeatIntervalEdit = nullptr;
    QTimer *m_repeatTimer = nullptr;
    bool m_repeatToDevice = true;
};

}  // namespace aether
