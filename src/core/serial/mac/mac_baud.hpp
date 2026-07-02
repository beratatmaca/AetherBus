/**
 * @file mac_baud.hpp
 * @brief Arbitrary (non-standard) baud-rate support for macOS.
 *
 * Standard rates map to termios @c Bxxxx constants, but custom rates require the
 * macOS @c IOSSIOSPEED ioctl from @c <IOKit/serial/ioss.h>. That header is
 * isolated in @c mac_baud.cpp and exposed through this tiny C++ wrapper, keeping
 * the IOKit dependency out of the shared POSIX proxy.
 */
#pragma once

namespace aether {

// macOS implementation of aether::setCustomBaud (declared and documented in
// linux_baud.hpp). Applies the exact baud rate via the IOSSIOSPEED ioctl, which
// must run after tcsetattr so the standard-speed fields do not overwrite it.
// Only one platform's translation unit is compiled per build.
bool setCustomBaud(int fd, int baud);

}  // namespace aether
