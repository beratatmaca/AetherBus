/**
 * @file i_bus_backend.hpp
 * @brief Transport-neutral backend interface shared by every capture source.
 *
 * Views and the session controller depend only on this interface and on
 * @ref aether::CapturedChunk — never on a concrete driver. The serial PTY proxy
 * and the SocketCAN backend both implement it, which keeps the GUI decoupled
 * from platform I/O (loose-coupling mandate).
 *
 * Transport-specific configuration (a @c SerialConfig vs a @c CanConfig) is
 * intentionally NOT part of this interface: the owner knows the concrete type
 * and calls the matching @c open() overload directly. This avoids a leaky
 * config union while still letting the common capture/lifecycle surface be used
 * polymorphically.
 */
#pragma once

#include "core/serial/serial_types.hpp"

#include <QObject>
#include <QString>

namespace aether {

class IBusBackend : public QObject {
    Q_OBJECT

public:
    explicit IBusBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~IBusBackend() override = default;

    IBusBackend(const IBusBackend &) = delete;
    IBusBackend &operator=(const IBusBackend &) = delete;

    /// Stop capture and release all resources. Idempotent.
    virtual void close() = 0;

    /// @return true while the backend is actively capturing.
    [[nodiscard]] virtual bool isRunning() const = 0;

signals:
    /// A timestamped, direction-tagged unit of traffic (bytes or a framed message).
    void chunkCaptured(const aether::CapturedChunk &chunk);

    /// Capture started; @p info is a human-readable description (slave PTY path
    /// for serial, or an interface summary such as "can0 @ 500000" for CAN).
    void started(const QString &info);

    /// Capture stopped cleanly (via @ref close).
    void stopped();

    /// A recoverable or fatal error occurred; @p message is human-readable.
    void errorOccurred(const QString &message);

    /// The underlying device/interface disappeared unexpectedly.
    void disconnected();
};

}  // namespace aether
