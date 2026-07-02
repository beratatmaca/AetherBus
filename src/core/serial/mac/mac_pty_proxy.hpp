#pragma once
#include "core/serial/posix/posix_pty_proxy.hpp"

namespace aether {

/**
 * @brief macOS specialisation of the shared POSIX PTY proxy.
 *
 * Identical to @ref PosixPtyProxy except that non-standard baud rates are
 * applied through the macOS @c IOSSIOSPEED ioctl.
 */
class MacPtyProxy : public PosixPtyProxy {
public:
    explicit MacPtyProxy(PtyProxy *q);
    ~MacPtyProxy() override;

protected:
    bool applyCustomBaud(int fd, int baud) override;
};

}  // namespace aether
