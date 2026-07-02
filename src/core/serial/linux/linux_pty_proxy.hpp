#pragma once
#include "core/serial/posix/posix_pty_proxy.hpp"

namespace aether {

/**
 * @brief Linux specialisation of the shared POSIX PTY proxy.
 *
 * Identical to @ref PosixPtyProxy except that non-standard baud rates are
 * applied through the Linux @c termios2 / @c BOTHER interface.
 */
class LinuxPtyProxy : public PosixPtyProxy {
public:
    explicit LinuxPtyProxy(PtyProxy *q);
    ~LinuxPtyProxy() override;

protected:
    bool applyCustomBaud(int fd, int baud) override;
};

}  // namespace aether
