/**
 * @file session_view.hpp
 * @brief Abstract tab contents hosted by MainWindow.
 *
 * Lets the main window manage serial and CAN sessions uniformly (title, close,
 * running-state) without depending on either concrete widget. Concrete session
 * widgets embed the shared display/stats components and own a transport backend.
 */
#pragma once

#include "core/serial/serial_types.hpp"

#include <QVector>
#include <QWidget>

#include <cstdint>

class QSettings;
class QJsonObject;

namespace aether {

/** @brief Which transport a session drives. */
enum class SessionType : std::uint8_t {
    Serial,  ///< UART interception via the PTY proxy.
    Can,     ///< SocketCAN receive/transmit.
    Ethernet, ///< Raw Ethernet packet capture/crafting.
    Usb,      ///< USB packet sniffing/capture.
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

    // --- Control API surface (see gui/control/control_server.hpp) ---------
    // Kept transport-agnostic here so ControlServer never depends on concrete
    // session widgets or backends.

    /** @brief Stable id assigned by MainWindow; addresses this session over the control socket. */
    [[nodiscard]] int controlId() const { return m_controlId; }
    void setControlId(int id) { m_controlId = id; }

    /** @brief Human-readable session name for the control `list` reply (defaults to the tab title). */
    [[nodiscard]] virtual QString sessionName() const { return objectName(); }

    /**
     * @brief Execute a control `send` command decoded from JSON, delegating to this
     * session's own backend. @p cmd carries transport-specific fields (serial: `side`,
     * `data`; CAN: `frameId`, `flags`, `data`; Ethernet: `data`).
     * @return false with a human-readable @p error on bad args or an inactive session.
     */
    virtual bool sendControl(const QJsonObject &cmd, QString *error) = 0;

signals:
    /** @brief Request the hosting tab's label be updated. */
    void sessionTitleChanged(const QString &title);

    /**
     * @brief Traffic captured by this session, mirrored to any subscribed control
     * clients. Re-emitted from the concrete widget's existing batched-chunk path;
     * cheap when nothing is connected.
     */
    void controlTraffic(const QVector<aether::CapturedChunk> &chunks);

    /**
     * @brief Status/error text for this session, mirrored into the main window's
     * always-visible status bar.
     *
     * @p isError marks messages that should persist.
     */
    void statusMessage(const QString &text, bool isError);

private:
    int m_controlId = 0;  ///< Assigned by MainWindow::addSession; 0 = unregistered.
};

}  // namespace aether
