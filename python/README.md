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

## Scripting a running GUI (control channel)

With the control channel enabled, a script can enumerate the open sessions,
send data/frames into them, and stream their captured traffic — over a local
socket (a Unix domain socket / Windows named pipe; owner-only, no network).

Enable it in one of two ways:

- launch with `aetherbus --control`, or
- toggle **Window → Enable Control Channel** in the GUI (remembered across
  restarts).

It is **off by default**: nothing listens until you enable it.

```python
import aetherbus

c = aetherbus.connect()                     # finds the running GUI's socket
print(c.hello)                              # {"protocol": 1, "version": "..."}

for s in c.sessions():
    print(s["id"], s["type"], s["name"], s["running"])

# Serial: send toward the device (default) or the target app side.
c.send(1, b"AT\r\n", side="device")

# CAN: pass a frame id (and optional flags).
c.send(2, b"\xde\xad\xbe\xef", frame_id=0x123)

# Ethernet: data is a full raw frame.
c.send(3, bytes.fromhex("ffffffffffff..."))

# Stream captured traffic (use a separate connection if you also send).
stream = aetherbus.connect()
for msg in stream.stream(1):
    print(msg["dir"], msg["data"])          # data is bytes
```

**Notes.** Use a dedicated `connect()` for `stream()` if you also issue commands
concurrently. The snap build runs under strict confinement, which relocates the
socket inside the sandbox — the control client works against the pip / DEB /
source builds, not the confined snap. See `--no-control` to force it off even if
a saved toggle would enable it.

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
