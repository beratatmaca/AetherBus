#pragma once

#include "core/ethernet/ethernet_backend.hpp"
#include "core/ethernet/ethernet_types.hpp"
#include "core/common/stats_calculator.hpp"
#include "gui/widgets/statspanel.hpp"
#include "gui/sessions/session_view.hpp"
#include "gui/sessions/ethernet_packet_model.hpp"
#include "gui/panels/packet_constructor_panel.hpp"

#include <QWidget>
#include <QTableView>
#include <QTreeWidget>
#include <QTextEdit>
#include <QSplitter>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>

class QSettings;

namespace aether {

class EthernetSessionWidget : public SessionView {
    Q_OBJECT

public:
    explicit EthernetSessionWidget(QWidget *parent = nullptr);
    ~EthernetSessionWidget() override;

    [[nodiscard]] bool isRunning() const override;
    void stopSession() override;
    [[nodiscard]] SessionType sessionType() const override { return SessionType::Ethernet; }
    void saveSettings(QSettings &settings) const override;
    void loadSettings(const QSettings &settings) override;

private slots:
    void startCapture();
    void stopCapture();
    void rescanInterfaces();
    
    void onStarted(const QString &info);
    void onStopped();
    void onError(const QString &message);
    void onDisconnected();
    void onThrottleTimeout();
    
    void onPacketReady(const QByteArray &packet);
    void onPacketSelected(const QModelIndex &current, const QModelIndex &previous);

    // Playback loop using libpcap
    void onPlayPcapRequested(const QString &filePath);
    void onStopPlaybackRequested();
    void onReplayTick();
    void onSavePcap();
    void onClearLog();

private:
    void buildUi();
    void processCapturedPacket(const aether::CapturedChunk &chunk);
    void parsePacket(const QByteArray &data, QTreeWidgetItem *root);

    EthernetBackend *m_backend = nullptr;
    PacketConstructorPanel *m_constructor = nullptr;

    // Interface widgets
    QComboBox *m_interfaceBox = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QPushButton *m_startBtn = nullptr;

    // Split views
    QTableView *m_packetList = nullptr;
    EthernetPacketModel *m_packetModel = nullptr;
    QTreeWidget *m_detailTree = nullptr;
    QTextEdit *m_hexDump = nullptr;
    QSplitter *m_decodersSplitter = nullptr;

    QTimer *m_pcapPlayTimer = nullptr;
    QTimer *m_throttleTimer = nullptr;
    QTimer *m_statsTimer = nullptr;

    // PCAP file replay: frames pending injection and the index of the next one.
    QVector<CapturedChunk> m_replayChunks;
    int m_replayIndex = 0;
    static constexpr int kMaxReplayGapMs = 2000;

    StatsCalculator m_stats;
    StatsPanel *m_statsPanel = nullptr;
};

} // namespace aether
