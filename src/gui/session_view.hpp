/**
 * @file session_view.h
 * @brief Abstract tab contents hosted by MainWindow.
 *
 * Lets the main window manage serial and CAN sessions uniformly (title, close,
 * running-state) without depending on either concrete widget. Concrete session
 * widgets embed the shared display/stats components and own a transport backend.
 */
#pragma once

#include <QWidget>

#include <cstdint>

namespace aether {

/// Which transport a session drives.
enum class SessionType : std::uint8_t {
    Serial,  ///< UART interception via the PTY proxy.
    Can,     ///< SocketCAN receive/transmit.
};

class SessionView : public QWidget {
    Q_OBJECT

public:
    explicit SessionView(QWidget *parent = nullptr) : QWidget(parent) {}
    ~SessionView() override = default;

    /// @return true while the session's backend is actively capturing.
    [[nodiscard]] virtual bool isRunning() const = 0;

    /// Stop the session's backend if running (used before closing the tab).
    virtual void stopSession() = 0;

signals:
    /// Request the hosting tab's label be updated.
    void sessionTitleChanged(const QString &title);
};

}  // namespace aether
