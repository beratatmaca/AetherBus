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

/**
 * @brief Apply an exact baud rate to an already-opened tty fd on macOS.
 *
 * Uses the @c IOSSIOSPEED ioctl, which must be applied after @c tcsetattr so it
 * is not overwritten by the standard-speed fields.
 * @param fd   An open tty file descriptor.
 * @param baud Desired baud rate in bits/second.
 * @return @c true on success; @c false on non-macOS builds or ioctl failure.
 */
bool setCustomBaud(int fd, int baud);

}  // namespace aether
