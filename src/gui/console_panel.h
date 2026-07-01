#pragma once

#include "gui/consoleview.h"
#include <QWidget>

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QFrame;
class QHBoxLayout;

namespace aether {

class ConsolePanel : public QWidget {
    Q_OBJECT

public:
    explicit ConsolePanel(QWidget *parent = nullptr);
    ~ConsolePanel() override;

    [[nodiscard]] ConsoleView *console() const { return m_console; }

    // Format states
    [[nodiscard]] bool isHexChecked() const;
    [[nodiscard]] bool isDecChecked() const;
    [[nodiscard]] bool isBinChecked() const;
    [[nodiscard]] bool isAsciiChecked() const;
    [[nodiscard]] bool isTimeChecked() const;
    [[nodiscard]] bool isPausedChecked() const;
    [[nodiscard]] bool isAutoScrollChecked() const;

    void setHexChecked(bool checked);
    void setDecChecked(bool checked);
    void setBinChecked(bool checked);
    void setAsciiChecked(bool checked);
    void setTimeChecked(bool checked);
    void setPausedChecked(bool checked);
    void setAutoScrollChecked(bool checked);

    // Section visibility
    void setSplitControlsVisible(bool visible);
    void setExtraActionsVisible(bool visible);
    void setSelectionLabelVisible(bool visible);

    // Widget getters
    [[nodiscard]] QComboBox *newlineModeBox() const { return m_newlineModeBox; }
    [[nodiscard]] QComboBox *newlineFormatBox() const { return m_newlineFormatBox; }
    [[nodiscard]] QLineEdit *newlineParamEdit() const { return m_newlineParamEdit; }
    [[nodiscard]] QLabel *countsLabel() const { return m_countsLabel; }
    [[nodiscard]] QLabel *selectionLabel() const { return m_selLabel; }
    [[nodiscard]] QPushButton *logButton() const { return m_logBtn; }
    [[nodiscard]] QPushButton *captureButton() const { return m_captureBtn; }
    [[nodiscard]] QPushButton *replayButton() const { return m_replayBtn; }

    void setCountsText(const QString &text);
    void setSelectionText(const QString &text);

signals:
    void formatChanged();
    void saveRequested();
    void logToggled(bool checked);
    void captureToggled(bool checked);
    void replayToggled(bool checked);
    void newlineModeChanged();

private slots:
    void updateConsoleFormats();
    void doFind(bool backward);

private:
    ConsoleView *m_console = nullptr;

    QPushButton *m_hexCheck = nullptr;
    QPushButton *m_decCheck = nullptr;
    QPushButton *m_binCheck = nullptr;
    QPushButton *m_asciiCheck = nullptr;

    // Split mode controls
    QWidget *m_splitContainer = nullptr;
    QComboBox *m_newlineModeBox = nullptr;
    QComboBox *m_newlineFormatBox = nullptr;
    QLineEdit *m_newlineParamEdit = nullptr;

    QPushButton *m_autoScrollCheck = nullptr;
    QPushButton *m_pauseCheck = nullptr;
    QPushButton *m_tsCheck = nullptr;

    QLabel *m_countsLabel = nullptr;
    QLabel *m_selLabel = nullptr;

    // Action buttons
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_logBtn = nullptr;
    QPushButton *m_captureBtn = nullptr;
    QPushButton *m_replayBtn = nullptr;
    QWidget *m_extraActionsContainer = nullptr;

    // Find bar controls
    QLineEdit *m_findEdit = nullptr;
    QLabel *m_matchCountLabel = nullptr;
};

}  // namespace aether
