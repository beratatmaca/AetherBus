# AetherBus

[![Build Status](https://github.com/beratatmaca/AetherBus/actions/workflows/ci.yml/badge.svg)](https://github.com/beratatmaca/AetherBus/actions)
[![Release Status](https://github.com/beratatmaca/AetherBus/actions/workflows/release.yml/badge.svg)](https://github.com/beratatmaca/AetherBus/actions)
[![codecov](https://codecov.io/gh/beratatmaca/AetherBus/graph/badge.svg)](https://codecov.io/gh/beratatmaca/AetherBus)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/compiler_support/17)
[![Qt 6](https://img.shields.io/badge/Qt-6-green.svg?logo=qt)](https://www.qt.io/)
[![aetherbus](https://snapcraft.io/aetherbus/badge.svg)](https://snapcraft.io/aetherbus)

AetherBus is a desktop tool for working with serial ports, SocketCAN buses, and raw Ethernet traffic from one app. It's written in C++17 with Qt 6, and it's built to keep up with high-baud/high-packet-rate traffic without dropping data or freezing the UI.

> [!NOTE]
> The app's metadata uses neutral wording (bridging, monitoring, viewing, generating) so it isn't flagged by Linux store review bots.

---

## Previews

### Dark Theme

<p align="center">
  <img src="assets/serial_session_dark.jpg" alt="Serial Utility Dark Theme" width="49%">
  <img src="assets/socketcan_session_dark.jpg" alt="SocketCAN Analyzer Dark Theme" width="49%">
</p>

### Light Theme

<p align="center">
  <img src="assets/serial_session_light.jpg" alt="Serial Utility Light Theme" width="49%">
  <img src="assets/socketcan_session_light.jpg" alt="SocketCAN Analyzer Light Theme" width="49%">
</p>

---

## Quick Start

### Install

* **Linux (Snap)**:

  ```bash
  sudo snap install aetherbus
  sudo snap connect aetherbus:serial-port
  sudo snap connect aetherbus:raw-usb
  sudo snap connect aetherbus:network-control
  sudo snap connect aetherbus:network-observe
  ```

* **Linux (Debian/Ubuntu)**: grab the `.deb` from [Releases](https://github.com/beratatmaca/AetherBus/releases).
* **macOS / Windows**: use the `.dmg` or `.msi` from [Releases](https://github.com/beratatmaca/AetherBus/releases).

### A serial session

1. Launch AetherBus, pick a device (`/dev/ttyUSB0`, `COM1`, ...), set the connection parameters, and hit **Start Interception**. The virtual port (e.g. `/dev/pts/5`) shows up in the status bar.
2. Point your target app (minicom, a flasher, test scripts) at that virtual port instead of the real device.
3. Watch traffic in the console, color-coded by direction, and use the injection panel to send your own bytes.

### CAN, Ethernet, and USB

* **File → New CAN Session**: pick an interface (`vcan0`, ...), **Start Capture**, get a live per-ID table plus DBC decoding.
* **File → New Ethernet Session**: pick an interface, **Start Capture**, get BPF-filtered live traffic.
* **File → New USB Session**: pick a USB interface (e.g. `usbmon1`, `USBPcap2`) and **Start Capture** to inspect raw URBs and control requests.

  > [!NOTE]
  > Linux USB sniffing requires the `usbmon` module to be loaded and read permissions on device nodes:
>
  > ```bash
  > sudo modprobe usbmon
  > sudo chmod +r /dev/usbmon*
  > ```

---

## Features

**Serial**
Bridges a physical UART through a PTY (or named pipe on Windows) so a client app talks to the virtual port while AetherBus mirrors both directions. Line settings (baud/parity/framing) changed by the client get mirrored onto the real device automatically. Inject HEX/ASCII/DEC/BIN payloads to either side, drive RTS/DTR/BREAK, watch CTS/DSR/DCD/RI live, and save frequent payloads as macros.

**SocketCAN** (Linux)
Classic CAN and CAN-FD over `PF_CAN` sockets. A per-ID table with changed-byte highlighting and stale-row dimming, ID+mask filters from the GUI, and DBC file loading to decode raw frames into named signals in real time.

**Ethernet**
`libpcap`-based capture with standard BPF filters. A Wireshark-style packet list, protocol tree, and hex/ASCII dump kept in sync. A packet constructor for crafting Ethernet II/IPv4/UDP/ICMP frames (checksums computed for you), one-off or periodic sends, and pcap save/replay paced by original packet timing.

**USB**
`libpcap`-based raw USB packet sniffing (`usbmon` on Linux, `USBPcap` on Windows). Real-time parser for USB Request Blocks (URBs) decoding descriptors (Device, Configuration, etc.) and control request commands.

**Across all four**
Side-by-side HEX/ASCII/BIN/DEC console rendering that stays fast at megabaud rates, live throughput graphs and inter-packet gap stats, a byte inspector for decoding selected bytes as int8–64/float/double, and multiple sessions running side by side in tabs or a tiled grid.

**Scripting**
An optional [`aetherbus`](https://pypi.org/project/aetherbus/) Python package (`pip install aetherbus`) can drive a running instance over a local socket — open/configure/start/stop sessions, inject data, stream traffic, capture, and run macros, for headless test automation. Enable it with `aetherbus --control` or **Window → Enable Control Channel**; the full API reference is in-app under **Help → Python API**.

---

## How it works

AetherBus sits between your client app and the target hardware, proxying both directions:

```text
                     ┌──────────────────────────────────┐
                     │        Target Application        │
                     │   (minicom, flasher, your app)   │
                     └──────────────┬───────────────────┘
                                    │  reads/writes /dev/pts/N
                                    ▼
    ┌─────────────────┐   poll()  ┌──────────────────────────┐
    │ Physical UART   │ ◀───────▶ │      AetherBus Proxy     │
    │ /dev/ttyUSB0    │   (Rx/Tx) │  PTY master ◀▶ slave pair│
    └─────────────────┘           └──────────────┬───────────┘
                                                 │  CapturedChunk queue
                                                 ▼
                                  ┌──────────────────────────┐
                                  │          Qt6 View        │
                                  │  HEX / ASCII / BIN / DEC │
                                  │   + byte injection panel │
                                  └──────────────────────────┘
```

---

## Building

### Prerequisites

* CMake 3.16+
* Qt6 (Core, Widgets, Network, Test)
* A C++17 compiler (GCC 10+, Clang 12+, MSVC 2019+)
* `libpcap-dev` (Linux/macOS, optional — needed for Ethernet and USB sessions)

### Build

```bash
git clone https://github.com/beratatmaca/AetherBus.git
cd AetherBus

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# with tests
cmake -B build -DBUILD_TESTING=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### Other targets

```bash
cmake --build build --target format   # clang-format
cmake --build build --target tidy     # clang-tidy
cmake --build build --target docs     # Doxygen
```

---

## License

MIT. See [LICENSE.txt](LICENSE.txt).

AetherBus links dynamically against Qt 6 (LGPL v3) — Qt is not statically linked, its source is available from [qt.io](https://www.qt.io/download), and you're free to relink your own build against a modified Qt per the LGPL v3 terms.
