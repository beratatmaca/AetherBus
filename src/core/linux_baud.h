/**
 * @file linux_baud.h
 * @brief Arbitrary (non-standard) baud-rate support for Linux.
 *
 * Standard rates map to termios @c Bxxxx constants, but custom rates require
 * the Linux @c termios2 + @c BOTHER ioctl path. That path needs
 * @c <asm/termbits.h>, whose definitions clash with @c <termios.h>, so it is
 * isolated in @c linux_baud.cpp and exposed through this tiny C++ wrapper.
 */
#pragma once

namespace aether {

/**
 * @brief Apply an exact baud rate to an already-opened tty fd.
 *
 * Uses the Linux @c termios2 / @c BOTHER interface and preserves all other line
 * settings.
 * @param fd   An open tty file descriptor.
 * @param baud Desired baud rate in bits/second.
 * @return @c true on success; @c false on non-Linux builds or ioctl failure.
 */
bool setCustomBaud(int fd, int baud);

}  // namespace aether
