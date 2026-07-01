#pragma once

#include "core/can_types.h"
#include <QGroupBox>

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;

namespace aether {

/// Left-hand configuration panel for a SocketCAN session: interface selection,
/// per-socket options, and a receive-filter editor. Emits a validated
/// @ref CanConfig when the user starts capture.
class CanConfigPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit CanConfigPanel(QWidget *parent = nullptr);
    ~CanConfigPanel() override;

    void setRunningState(bool running);
    void setStatus(const QString &htmlText);
    void populateInterfaces(const QStringList &ifaces);
    [[nodiscard]] QString iface() const;

signals:
    void startCan(const CanConfig &cfg);
    void stopCan();
    void rescanRequested();

private slots:
    void onStartButtonClicked();

private:
    /// Parse the UI into @p out. Returns false (and sets an error status) on a
    /// malformed filter specification.
    bool buildConfig(CanConfig &out);

    QComboBox *m_ifaceBox = nullptr;
    QCheckBox *m_fdCheck = nullptr;
    QCheckBox *m_loopbackCheck = nullptr;
    QCheckBox *m_recvOwnCheck = nullptr;
    QCheckBox *m_errorCheck = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QPushButton *m_startButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    bool m_isRunning = false;
};

}  // namespace aether
