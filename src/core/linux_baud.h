// Arbitrary (non-standard) baud-rate support.
//
// Standard rates map to termios `Bxxxx` constants, but custom rates require the
// Linux `termios2` + `BOTHER` ioctl path. That path needs <asm/termbits.h>,
// whose definitions clash with <termios.h>, so it is isolated in its own
// translation unit (linux_baud.cpp) and exposed through this tiny C++ wrapper.
#pragma once

namespace aether {

/// Apply an exact baud rate to an already-opened tty fd using the Linux
/// `termios2`/`BOTHER` interface. Preserves all other line settings. Returns
/// false on non-Linux builds or if the ioctl fails.
bool setCustomBaud(int fd, int baud);

}  // namespace aether
