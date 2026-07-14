# AetherBus

Modern lightweight serial-port interceptor and protocol sniffer GUI — an
interceptty-style man-in-the-middle proxy with an HTerm-style diagnostic
interface, plus SocketCAN and raw Ethernet sessions.

This package ships the compiled Qt 6 desktop application together with its Qt
runtime; no Qt installation is required. Source code, full documentation, and
native installers (DEB/MSI/DMG/Snap) live at
[github.com/beratatmaca/AetherBus](https://github.com/beratatmaca/AetherBus).

## Install & run

Wheels are currently built as CI artifacts of the
[AetherBus Python Wheels workflow](https://github.com/beratatmaca/AetherBus/actions/workflows/wheels.yml)
(PyPI publishing is not wired up yet). Download the wheel for your platform
and:

```sh
pip install ./aetherbus-*.whl
aetherbus
```

`aetherbus --version` prints the installed version. Any other arguments are
forwarded to the application.

## Supported platforms

Wheels are built for Linux x86_64 (glibc 2.35+, e.g. Ubuntu 22.04+,
Debian 12+, Fedora 36+), Windows x64, and macOS arm64 (macOS 12+). There is no
source distribution — building from source needs the Qt 6 SDK; see the GitHub
repository for instructions.

## Platform notes

- **Linux**: the wheel bundles Qt but relies on the usual desktop system
  libraries (X11/xcb, xkbcommon, fontconfig, OpenGL). On a minimal system:
  `apt install libgl1 libxkbcommon-x11-0 libxcb-cursor0` (or your distro's
  equivalents). Serial interception uses PTYs and needs read/write access to
  the target device (typically the `dialout` group).
- **Windows**: serial and CAN features work out of the box. Raw Ethernet
  capture additionally requires [Npcap](https://npcap.com/) to be installed
  system-wide.
- **macOS**: the bundled app is unsigned. Installing via `pip` works as-is;
  if you instead download the wheel manually with a browser, Gatekeeper may
  quarantine it.
