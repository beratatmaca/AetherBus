#include "core/serial/linux_baud.hpp"

// IMPORTANT: this translation unit must NOT include <termios.h>; <asm/termbits.h>
// provides struct termios2 / BOTHER / TCSETS2 and conflicts with it.
#if defined(__linux__)
#include <asm/termbits.h>
#include <sys/ioctl.h>
#endif

namespace aether {

bool setCustomBaud(int fd, int baud) {
#if defined(__linux__)
    if (baud <= 0) {
        return false;
    }
    struct termios2 tio {};
    if (::ioctl(fd, TCGETS2, &tio) != 0) {
        return false;
    }
    tio.c_cflag &= ~static_cast<tcflag_t>(CBAUD);
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = static_cast<speed_t>(baud);
    tio.c_ospeed = static_cast<speed_t>(baud);
    return ::ioctl(fd, TCSETS2, &tio) == 0;
#else
    (void)fd;
    (void)baud;
    return false;
#endif
}

}  // namespace aether
