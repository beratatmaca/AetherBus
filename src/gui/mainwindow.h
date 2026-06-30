// Top-level diagnostic window: serial configuration controls, the HTerm-style
// console with its display toolbar, and a direct byte-injection panel, all
// wired to a PtyProxy.
#pragma once

#include "core/serial_types.h"

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QTimer;

namespace aether {

class PtyProxy;
class ConsoleView;
class ThemeController;
class MacroBar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void toggleProxy();
    void onStarted(const QString &slavePath);
    void onStopped();
    void onError(const QString &message);
    void applyFormats();
    void applyNewlineMode();
    void updateCounts(qint64 rx, qint64 tx);
    void saveReceived();
    void toggleLogging();
    void sendFile();
    void pollModemLines();
    void onDisconnected();

private:
    void buildUi();
    QWidget *buildConfigPanel(QWidget *parent);
    QWidget *buildConsolePanel(QWidget *parent);
    QWidget *buildSignalPanel(QWidget *parent);
    void populateDevices();
    void setRunningState(bool running);
    QByteArray encodeInjection(bool &ok);
    void sendInjection(bool toDevice);
    void doFind(bool backward);

    PtyProxy *m_proxy;
    ConsoleView *m_console;
    ThemeController *m_theme = nullptr;

    // Connection settings.
    QComboBox *m_deviceBox = nullptr;
    QComboBox *m_baudBox = nullptr;
    QComboBox *m_dataBitsBox = nullptr;
    QComboBox *m_parityBox = nullptr;
    QComboBox *m_stopBitsBox = nullptr;
    QComboBox *m_flowBox = nullptr;
    QLineEdit *m_symlinkEdit = nullptr;
    QPushButton *m_startButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    QCheckBox *m_hexCheck = nullptr;
    QCheckBox *m_decCheck = nullptr;
    QCheckBox *m_binCheck = nullptr;
    QCheckBox *m_asciiCheck = nullptr;
    QComboBox *m_newlineModeBox = nullptr;
    QLineEdit *m_newlineParamEdit = nullptr;
    QCheckBox *m_autoScrollCheck = nullptr;
    QCheckBox *m_pauseCheck = nullptr;
    QLabel *m_countsLabel = nullptr;
    QLineEdit *m_findEdit = nullptr;

    QLabel *m_selLabel = nullptr;
    QPushButton *m_logBtn = nullptr;

    // Injection panel.
    QLineEdit *m_injectEdit = nullptr;
    QComboBox *m_injectFormatBox = nullptr;
    QComboBox *m_injectEndingBox = nullptr;
    QPushButton *m_toAppBtn = nullptr;
    QCheckBox *m_directCheck = nullptr;
    QCheckBox *m_repeatCheck = nullptr;
    QLineEdit *m_repeatIntervalEdit = nullptr;
    QTimer *m_repeatTimer = nullptr;
    bool m_repeatToDevice = true;
    MacroBar *m_macroBar = nullptr;

    // Signal control + modem-line status.
    QCheckBox *m_rtsCheck = nullptr;
    QCheckBox *m_dtrCheck = nullptr;
    QLabel *m_ctsLed = nullptr;
    QLabel *m_dsrLed = nullptr;
    QLabel *m_dcdLed = nullptr;
    QLabel *m_riLed = nullptr;
    QTimer *m_modemTimer = nullptr;

    // Auto-reconnect.
    QCheckBox *m_reconnectCheck = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    SerialConfig m_lastConfig;
};

}  // namespace aether
