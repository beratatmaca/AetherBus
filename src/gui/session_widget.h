#pragma once

#include "core/serial_types.h"
#include "core/stats_calculator.h"
#include <QWidget>

class QTimer;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;

namespace aether {

class PtyProxy;
class ConsoleView;
class MacroBar;
class StatsPanel;
class CaptureReplayer;

class ConfigPanel;
class SignalPanel;
class InjectionPanel;

class SessionWidget : public QWidget {
    Q_OBJECT

public:
    explicit SessionWidget(QWidget *parent = nullptr);
    ~SessionWidget() override;

    bool isRunning() const;
    void stopSession();

    StatsCalculator &stats() { return m_stats; }

signals:
    void sessionTitleChanged(const QString &title);

private slots:
    void startInterception(const SerialConfig &cfg);
    void stopInterception();
    void rescanDevices();

    void onStarted(const QString &slavePath);
    void onStopped();
    void onError(const QString &message);
    void applyFormats();
    void applyNewlineMode();
    void updateCounts(qint64 rx, qint64 tx, qint64 rxRate, qint64 txRate);
    void saveReceived();
    void toggleLogging();
    void sendFile();
    void pollModemLines();
    void onDisconnected();
    void toggleCapture();
    /// Open a captured pcap file and replay it through the console (offline analysis).
    void toggleReplay();
    /// Restore UI state once a replay reaches the end.
    void onReplayFinished();
    /// Reflect a slave-PTY line-setting change the backend mirrored to the device.
    void onLineReconfigured(int baud, int dataBits, aether::Parity parity, int stopBits);
    /// Warn that a write queue overflowed and bytes were dropped.
    void onWriteStalled(aether::Direction dir, quint64 droppedTotal);
    void onChunkCaptured(const aether::CapturedChunk &chunk);

private:
    void buildUi();
    QWidget *buildConsolePanel(QWidget *parent);
    void doFind(bool backward);

    PtyProxy *m_proxy;
    ConsoleView *m_console{nullptr};

    ConfigPanel *m_configPanel = nullptr;
    SignalPanel *m_signalPanel = nullptr;
    InjectionPanel *m_injectPanel = nullptr;

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
    QCheckBox *m_tsCheck = nullptr;

    QLabel *m_selLabel = nullptr;
    QPushButton *m_logBtn = nullptr;
    QPushButton *m_captureBtn = nullptr;
    QPushButton *m_replayBtn = nullptr;  ///< offline capture-file replay toggle

    CaptureReplayer *m_replayer = nullptr;

    MacroBar *m_macroBar = nullptr;
    QTimer *m_modemTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    SerialConfig m_lastConfig;

    StatsCalculator m_stats;
    QTimer *m_statsTimer = nullptr;
    StatsPanel *m_statsPanel = nullptr;
};

}  // namespace aether
