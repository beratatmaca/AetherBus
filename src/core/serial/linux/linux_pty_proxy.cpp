#include "core/serial/linux/linux_pty_proxy.hpp"
#include "core/serial/linux_baud.hpp"

namespace aether {

LinuxPtyProxy::LinuxPtyProxy(PtyProxy *q) : PosixPtyProxy(q) {}

LinuxPtyProxy::~LinuxPtyProxy() = default;

bool LinuxPtyProxy::applyCustomBaud(int fd, int baud) {
    return setCustomBaud(fd, baud);
}

}  // namespace aether
