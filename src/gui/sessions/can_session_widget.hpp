#pragma once

#include "core/can/can_types.hpp"
#include "core/serial/serial_types.hpp"
#include "core/common/stats_calculator.hpp"
#include "gui/sessions/session_view.hpp"

class QLineEdit;
class QPushButton;
class QCheckBox;
class QLabel;
class QTimer;
class QComboBox;
class QHBoxLayout;

namespace aether {

class CanBackend;
class ConsoleView;
class ConsolePanel;
class StatsPanel;
class CanConfigPanel;
class CanDecoderPanel;
class CaptureReplayer;

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
    void toggleCapture();
    void toggleReplay();
    void onReplayFinished();
    void toggleLogging(bool checked);

private:
    void buildUi();
    QWidget *buildConsolePanel(QWidget *parent);

    CanBackend *m_backend;
    ConsolePanel *m_consolePanel = nullptr;
    CanConfigPanel *m_configPanel = nullptr;
    CaptureReplayer *m_replayer = nullptr;
    StatsPanel *m_statsPanel = nullptr;
    CanDecoderPanel *m_decoderPanel = nullptr;

    // Transmit bar.
    QComboBox *m_txFormatBox = nullptr;
    QLineEdit *m_txIdEdit = nullptr;
    QLineEdit *m_txDataEdit = nullptr;
    QCheckBox *m_txEffCheck = nullptr;
    QCheckBox *m_txRtrCheck = nullptr;
    QCheckBox *m_txFdCheck = nullptr;
    QCheckBox *m_txBrsCheck = nullptr;
    QPushButton *m_txButton = nullptr;

    StatsCalculator m_stats;
    QTimer *m_statsTimer = nullptr;

    struct CanHistoryItem {
        quint32 id = 0;
        QByteArray payload;
        quint16 flags = 0;
    };
    QVector<CanHistoryItem> m_txHistory;
    QComboBox *m_txHistoryBox = nullptr;

    struct CanMacro {
        QString name;
        quint32 id = 0;
        QByteArray payload;
        quint16 flags = 0;
    };
    QVector<CanMacro> m_macros;
    QWidget *m_macroContainer = nullptr;
    QHBoxLayout *m_macroLayout = nullptr;
    QLabel *m_emptyMacroHint = nullptr;

    void loadMacros();
    void saveMacros();
    void rebuildMacroButtons();
    void saveCurrentAsMacro();
};

}  // namespace aether
