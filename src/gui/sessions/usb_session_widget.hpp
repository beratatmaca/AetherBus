#pragma once

#include "core/usb/usb_types.hpp"
#include "core/serial/serial_types.hpp"
#include "core/common/stats_calculator.hpp"
#include "gui/sessions/session_view.hpp"
#include "core/usb/usb_pcap.hpp"

class QTimer;

namespace aether {

class UsbBackend;
class ConsolePanel;
class StatsPanel;
class UsbConfigPanel;
class UsbInjectionPanel;
class CollapsibleSplitter;

/**
 * @brief A USB monitor session tab, incorporating a live transaction log,
 * configuration controls, and real-time capture throughput statistics.
 */
class UsbSessionWidget : public SessionView {
    Q_OBJECT

public:
    explicit UsbSessionWidget(QWidget *parent = nullptr);
    ~UsbSessionWidget() override;

    [[nodiscard]] bool isRunning() const override;
    void stopSession() override;
    [[nodiscard]] SessionType sessionType() const override { return SessionType::Usb; }
    void saveSettings(QSettings &settings) const override;
    void loadSettings(const QSettings &settings) override;
    bool sendControl(const QJsonObject &cmd, QString *error) override;

private slots:
    void startCapture(const UsbConfig &cfg);
    void stopCapture();
    void rescan();
    void onStarted(const QString &info);
    void onStopped();
    void onError(const QString &message);
    void onDisconnected();
    void onChunksCaptured(const QVector<aether::CapturedChunk> &chunks);
    void applyFormats();
    void updateCounts();
    void toggleLogging();
    void toggleCapture();

private:
    void buildUi();
    QWidget *buildConsolePanel(QWidget *parent);

    UsbBackend *m_backend = nullptr;
    ConsolePanel *m_consolePanel = nullptr;
    UsbConfigPanel *m_configPanel = nullptr;
    UsbInjectionPanel *m_injectionPanel = nullptr;
    StatsPanel *m_statsPanel = nullptr;
    CollapsibleSplitter *m_mainSplitter = nullptr;

    StatsCalculator m_stats;
    QTimer *m_statsTimer = nullptr;
    UsbPcapWriter m_captureWriter;
};

}  // namespace aether
