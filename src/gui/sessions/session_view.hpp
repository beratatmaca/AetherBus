/**
 * @file session_view.hpp
 * @brief Abstract tab contents hosted by MainWindow.
 *
 * Lets the main window manage serial and CAN sessions uniformly (title, close,
 * running-state) without depending on either concrete widget. Concrete session
 * widgets embed the shared display/stats components and own a transport backend.
 */
#pragma once

#include <QWidget>

#include <cstdint>

class QSettings;

namespace aether {

/** @brief Which transport a session drives. */
enum class SessionType : std::uint8_t {
    Serial,  ///< UART interception via the PTY proxy.
    Can,     ///< SocketCAN receive/transmit.
    Ethernet, ///< Raw Ethernet packet capture/crafting.
};

class SessionView : public QWidget {
    Q_OBJECT

public:
    explicit SessionView(QWidget *parent = nullptr) : QWidget(parent) {}
    ~SessionView() override = default;

    /** @return true while the session's backend is actively capturing. */
    [[nodiscard]] virtual bool isRunning() const = 0;

    /** @brief Stop the session's backend if running (used before closing the tab). */
    virtual void stopSession() = 0;

    /**
     * @brief Which transport this session drives; lets the host window persist and
     * restore the workspace without knowing concrete session types.
     */
    [[nodiscard]] virtual SessionType sessionType() const = 0;

    /**
     * @brief Write this session's current configuration into @p settings, using keys
     * relative to whatever group/array index the caller has already selected.
     */
    virtual void saveSettings(QSettings &settings) const = 0;

    /**
     * @brief Restore configuration previously written by @ref saveSettings.
     *
     * Only pre-fills fields — never starts the underlying connection or capture.
     */
    virtual void loadSettings(const QSettings &settings) = 0;

signals:
    /** @brief Request the hosting tab's label be updated. */
    void sessionTitleChanged(const QString &title);

    /**
     * @brief Status/error text for this session, mirrored into the main window's
     * always-visible status bar.
     *
     * @p isError marks messages that should persist.
     */
    void statusMessage(const QString &text, bool isError);
};

}  // namespace aether
