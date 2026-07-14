#pragma once

#include "core/serial/serial_types.hpp"
#include "core/common/stats_calculator.hpp"
#include "gui/sessions/session_view.hpp"
#include <QWidget>

class QTimer;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;

namespace aether {

class PtyProxy;
class ConsoleView;
class ConsolePanel;
class MacroBar;
class StatsPanel;
class CaptureReplayer;

class ConfigPanel;
class SignalPanel;
class InjectionPanel;
class CollapsibleSplitter;

class SessionWidget : public SessionView {
    Q_OBJECT

public:
    explicit SessionWidget(QWidget *parent = nullptr);
    ~SessionWidget() override;

    [[nodiscard]] bool isRunning() const override;
    void stopSession() override;
    [[nodiscard]] SessionType sessionType() const override { return SessionType::Serial; }
    void saveSettings(QSettings &settings) const override;
    void loadSettings(const QSettings &settings) override;
    bool sendControl(const QJsonObject &cmd, QString *error) override;

    StatsCalculator &stats() { return m_stats; }

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
    /** @brief Open a captured pcap file and replay it through the console (offline analysis). */
    void toggleReplay();
    /** @brief Restore UI state once a replay reaches the end. */
    void onReplayFinished();
    /** @brief Reflect a slave-PTY line-setting change the backend mirrored to the device. */
    void onLineReconfigured(int baud, int dataBits, aether::Parity parity, int stopBits);
    /** @brief Warn that a write queue overflowed and bytes were dropped. */
    void onWriteStalled(aether::Direction dir, quint64 droppedTotal);
    void onChunksCaptured(const QVector<aether::CapturedChunk> &chunks);

private:
    void buildUi();
    QWidget *buildConsolePanel(QWidget *parent);

    PtyProxy *m_proxy;
    ConsolePanel *m_consolePanel = nullptr;

    ConfigPanel *m_configPanel = nullptr;
    SignalPanel *m_signalPanel = nullptr;
    InjectionPanel *m_injectPanel = nullptr;

    CaptureReplayer *m_replayer = nullptr;

    MacroBar *m_macroBar = nullptr;
    QTimer *m_modemTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    SerialConfig m_lastConfig;

    StatsCalculator m_stats;
    QTimer *m_statsTimer = nullptr;
    StatsPanel *m_statsPanel = nullptr;

    CollapsibleSplitter *m_mainSplitter = nullptr;
};

}  // namespace aether
