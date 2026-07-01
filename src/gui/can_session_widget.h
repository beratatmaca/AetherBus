#pragma once

#include "core/can_types.h"
#include "core/serial_types.h"
#include "core/stats_calculator.h"
#include "gui/session_view.h"

class QLineEdit;
class QPushButton;
class QCheckBox;
class QLabel;
class QTimer;

namespace aether {

class CanBackend;
class ConsoleView;
class StatsPanel;
class CanConfigPanel;

/// A SocketCAN session: candump-style frame log plus a cansend-style transmit
/// bar, reusing the shared ConsoleView and StatsPanel. Kept separate from the
/// serial SessionWidget so neither transport's UI complicates the other.
class CanSessionWidget : public SessionView {
    Q_OBJECT

public:
    explicit CanSessionWidget(QWidget *parent = nullptr);
    ~CanSessionWidget() override;

    [[nodiscard]] bool isRunning() const override;
    void stopSession() override;

private slots:
    void startCapture(const CanConfig &cfg);
    void stopCapture();
    void rescan();
    void onStarted(const QString &info);
    void onStopped();
    void onError(const QString &message);
    void onDisconnected();
    void onChunkCaptured(const aether::CapturedChunk &chunk);
    void transmit();
    void applyFormats();
    void updateCounts();

private:
    void buildUi();
    QWidget *buildConsolePanel(QWidget *parent);

    CanBackend *m_backend;
    ConsoleView *m_console = nullptr;
    CanConfigPanel *m_configPanel = nullptr;
    StatsPanel *m_statsPanel = nullptr;

    QPushButton *m_hexCheck = nullptr;
    QPushButton *m_decCheck = nullptr;
    QPushButton *m_binCheck = nullptr;
    QPushButton *m_asciiCheck = nullptr;
    QPushButton *m_autoScrollCheck = nullptr;
    QPushButton *m_pauseCheck = nullptr;
    QPushButton *m_tsCheck = nullptr;
    QLabel *m_countsLabel = nullptr;

    // Transmit bar.
    QLineEdit *m_txIdEdit = nullptr;
    QLineEdit *m_txDataEdit = nullptr;
    QCheckBox *m_txEffCheck = nullptr;
    QCheckBox *m_txRtrCheck = nullptr;
    QCheckBox *m_txFdCheck = nullptr;
    QCheckBox *m_txBrsCheck = nullptr;
    QPushButton *m_txButton = nullptr;

    StatsCalculator m_stats;
    QTimer *m_statsTimer = nullptr;
};

}  // namespace aether
