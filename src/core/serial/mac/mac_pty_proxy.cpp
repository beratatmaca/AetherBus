#include "core/serial/mac/mac_pty_proxy.hpp"
#include "core/serial/mac/mac_baud.hpp"

namespace aether {

MacPtyProxy::MacPtyProxy(PtyProxy *q) : PosixPtyProxy(q) {}

MacPtyProxy::~MacPtyProxy() = default;

bool MacPtyProxy::applyCustomBaud(int fd, int baud) {
    return setCustomBaud(fd, baud);
}

}  // namespace aether
