#include "core/serial/mac/mac_baud.hpp"

#if defined(__APPLE__)
#include <IOKit/serial/ioss.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

namespace aether {

bool setCustomBaud(int fd, int baud) {
#if defined(__APPLE__)
    if (baud <= 0) {
        return false;
    }
    // IOSSIOSPEED takes a speed_t by pointer and must run after tcsetattr, so the
    // standard-speed fields it just wrote do not clobber the custom rate.
    speed_t speed = static_cast<speed_t>(baud);
    return ::ioctl(fd, IOSSIOSPEED, &speed) != -1;
#else
    (void)fd;
    (void)baud;
    return false;
#endif
}

}  // namespace aether
