#pragma once

#include "core/can/can_types.hpp"
#include <QGroupBox>

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;
class QTableWidget;
class QSettings;

namespace aether {

/**
 * @brief Left-hand configuration panel for a SocketCAN session: interface selection,
 * per-socket options, and a receive-filter editor.
 *
 * Emits a validated
 * @ref CanConfig when the user starts capture.
 */
class CanConfigPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit CanConfigPanel(QWidget *parent = nullptr);
    ~CanConfigPanel() override;

    void setRunningState(bool running);
    void setStatus(const QString &htmlText);
    void populateInterfaces(const QStringList &ifaces);
    [[nodiscard]] QString iface() const;

    /** @brief Write the current field values to @p settings (relative keys). */
    void saveSettings(QSettings &settings) const;
    /** @brief Restore field values previously written by @ref saveSettings. */
    void loadSettings(const QSettings &settings);

    /** @brief Snapshot the current field values (best-effort; skips malformed filter rows). */
    [[nodiscard]] CanConfig currentConfig() const;
    /** @brief Populate fields from @p cfg, rebuilding the receive-filter table. */
    void applyConfig(const CanConfig &cfg);

signals:
    void startCan(const CanConfig &cfg);
    void stopCan();
    void rescanRequested();
    /**
     * @brief Emitted whenever the status text changes, as plain text plus an error flag,
     * so the host window can mirror it into the always-visible status bar.
     */
    void statusChanged(const QString &plainText, bool isError);

private slots:
    void onStartButtonClicked();

private:
    /**
     * @brief Parse the UI into @p out.
     *
     * Returns false (and sets an error status) on a
     * malformed filter specification.
     */
    bool buildConfig(CanConfig &out);

    void addFilterRow(bool use, const QString &idHex, const QString &maskHex, bool ext, bool invert);
    void loadFiltersFromString(const QString &spec);
    [[nodiscard]] QString saveFiltersToString() const;

    QComboBox *m_ifaceBox = nullptr;
    QCheckBox *m_fdCheck = nullptr;
    QCheckBox *m_loopbackCheck = nullptr;
    QCheckBox *m_recvOwnCheck = nullptr;
    QCheckBox *m_errorCheck = nullptr;
    QTableWidget *m_filterTable = nullptr;
    QPushButton *m_addFilterBtn = nullptr;
    QPushButton *m_removeFilterBtn = nullptr;
    QPushButton *m_clearFiltersBtn = nullptr;
    QPushButton *m_startButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    bool m_isRunning = false;
};

}  // namespace aether
