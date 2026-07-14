#pragma once

#include "core/ethernet/ethernet_backend.hpp"
#include "core/ethernet/ethernet_types.hpp"
#include "core/ethernet/ethernet_pcap.hpp"
#include "core/common/stats_calculator.hpp"
#include "gui/widgets/statspanel.hpp"
#include "gui/sessions/session_view.hpp"
#include "gui/sessions/ethernet_packet_model.hpp"
#include "gui/panels/packet_constructor_panel.hpp"
#include "gui/widgets/collapsible_splitter.hpp"

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

/**
 * @brief A raw Ethernet capture session: Wireshark-style packet list, per-packet
 * detail tree and hex dump, plus the packet constructor for transmit.
 */
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

    // Continuous save-to-disk, independent of the in-memory packet list.
    void toggleFileCapture();

    // Offline load-and-browse of a saved pcap file (distinct from "Play
    // PCAP…", which injects frames onto the wire).
    void toggleOfflineReplay();
    void onOfflineReplayTick();

private:
    void buildUi();
    void processCapturedPacket(const aether::CapturedChunk &chunk);
    void appendToPacketList(const QVector<aether::CapturedChunk> &chunks);
    void parsePacket(const QByteArray &data, QTreeWidgetItem *root);
    void renderSelectedHexDump();

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
    CollapsibleSplitter *m_decodersSplitter = nullptr;

    // Toolbar controls shared in spirit with the Serial/CAN console toolbar.
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_captureBtn = nullptr;
    QPushButton *m_replayBtn = nullptr;
    QPushButton *m_hexCheck = nullptr;
    QPushButton *m_decCheck = nullptr;
    QPushButton *m_binCheck = nullptr;
    QPushButton *m_asciiCheck = nullptr;

    QVector<CapturedChunk> m_pausedChunks;  ///< Packets received while paused; flushed into the model on resume.

    EthernetPcapWriter m_captureWriter;  ///< Continuous "Capture" toggle: streams every packet to disk as it
                                         ///< arrives, independent of the in-memory model's 10,000-row cap.

    QTimer *m_pcapPlayTimer = nullptr;
    QTimer *m_throttleTimer = nullptr;
    QTimer *m_statsTimer = nullptr;

    QVector<CapturedChunk> m_replayChunks;  ///< PCAP file replay: frames pending injection and the index of the next one.
    int m_replayIndex = 0;
    static constexpr int kMaxReplayGapMs = 2000;

    QTimer *m_offlineReplayTimer = nullptr;  ///< Offline "Replay" (view-only, distinct from the on-wire replay above).
    QVector<CapturedChunk> m_offlineReplayChunks;
    int m_offlineReplayIndex = 0;

    StatsCalculator m_stats;
    StatsPanel *m_statsPanel = nullptr;

    CollapsibleSplitter *m_mainSplitter = nullptr;
};

} // namespace aether
