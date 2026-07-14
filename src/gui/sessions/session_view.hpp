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
     * @brief Execute a control-channel @p verb against this session, delegating to its
     * own backend/widgets. @p args carries the verb's fields decoded from JSON:
     *   - `send`   — serial: `side`,`data`; CAN: `frameId`,`flags`,`data`; Ethernet: `data`.
     *   - `start`  — optional `config` object (applied first); `stop` — no args.
     *   - `stats`  — none; fills @p reply with `{rxBytes,txBytes,rxChunks,txChunks,rxRate,txRate,running}`.
     *   - `capture`— `action` (`start`|`stop`|`status`), `path` (for start); fills `reply["capturing"]`.
     *   - `replay` — `path`, optional `action` (`start`|`stop`).
     *   - `run_macro` — `name` or `index`; fills `reply["index"]`.
     * Verbs a transport does not support fail with a message rather than crashing.
     * @param reply out-parameter for verbs that return data (merged into the client reply).
     * @return false with a human-readable @p error on bad args or an inactive session.
     */
    virtual bool handleControl(const QString &verb, const QJsonObject &args,
                               QJsonObject &reply, QString *error) = 0;

    /**
     * @brief Start this session's backend using its current configuration (the same
     * path the Start/Connect button drives). Symmetric with @ref stopSession.
     * @return false with a human-readable @p error if it can't start.
     */
    virtual bool startSession(QString *error) = 0;

    /**
     * @brief Apply a control-channel config object to this session's widgets before
     * starting (transport-specific keys; see @ref handleControl). Only pre-fills
     * fields — never starts the backend.
     * @return false with a human-readable @p error on invalid config.
     */
    virtual bool applyControlConfig(const QJsonObject &config, QString *error) = 0;

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
