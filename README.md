# AetherBus

[![Build Status](https://github.com/beratatmaca/AetherBus/actions/workflows/ci.yml/badge.svg)](https://github.com/beratatmaca/AetherBus/actions)
[![Release Status](https://github.com/beratatmaca/AetherBus/actions/workflows/release.yml/badge.svg)](https://github.com/beratatmaca/AetherBus/actions)
[![codecov](https://codecov.io/gh/beratatmaca/AetherBus/graph/badge.svg)](https://codecov.io/gh/beratatmaca/AetherBus)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/compiler_support/17)
[![Qt 6](https://img.shields.io/badge/Qt-6-green.svg?logo=qt)](https://www.qt.io/)

AetherBus is a modern, lightweight, open-source serial-port interceptor, protocol sniffer, and SocketCAN bus analyzer for Linux, macOS, and Windows.

Written in C++17 and powered by the Qt 6 framework, it transparently proxies a physical UART through a virtual port — a kernel pseudo-terminal on Linux/macOS, or a named pipe on Windows — letting a target application keep talking to the device while AetherBus captures, decodes, and lets you inject every byte in real time. It records to Wireshark-compatible pcap, replays captures offline, mirrors live line-setting changes onto the hardware, and reports throughput and timing statistics. On Linux, an independent SocketCAN session type adds a `candump`/`cansend`-equivalent CAN bus analyzer — a live per-ID sniffer table, structured filters, CAN-FD, and DBC-driven signal decoding — in its own tab alongside serial sessions. Think `interceptty` wired to a diagnostic console, built for high-baud streams without dropping frames or stalling the UI.

## Screenshot

![AetherBus connected to the simulator, showing live Rx and Tx serial traffic](assets/aetherbus-main-window.png)

## How It Works

AetherBus sits transparently between your application and the hardware. It opens the real device, hands your application a virtual port (a pseudo-terminal), and shuttles every byte across while tagging its direction:

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

A parallel SocketCAN backend implements the same capture/lifecycle interface (`IBusBackend`) for CAN sessions, so the console, stats, and macro machinery are shared between serial and CAN — see [Technical Architecture](#technical-architecture).

## Features

**Serial interception**

* **Transparent interception** — proxies a real UART through a kernel PTY; the target app connects to a virtual port and never knows it is being watched.
* **Live line-setting mirroring** — when the target app reconfigures baud/parity/framing on the virtual port, AetherBus catches the change and applies it to the physical device automatically.
* **Byte injection** — send crafted HEX / ASCII / DEC / BIN sequences (optional CR/LF endings, one-shot or repeating) to either the device or the application side, plus reusable macros and send history.
* **Signal control** — drive RTS/DTR, send a serial BREAK, and watch the CTS/DSR/DCD/RI modem lines update live.

**SocketCAN bus analysis** (Linux only)

* **Independent CAN session type** — `candump`/`cansend`-equivalent capture and transmit over `PF_CAN`/`SOCK_RAW`, classic and CAN-FD, alongside serial sessions in their own tabs.
* **Live per-ID sniffer table** — one row per CAN ID (DLC, frame count, last-received time, hex payload) with changed-byte highlighting and stale-row graying, throttled to 5 Hz.
* **Structured filter table** — add/remove ID + mask rules with extended-ID and invert flags, instead of hand-typed filter strings.
* **DBC signal decoding** — load a `.dbc` file (or define custom rules) to see live decoded signal values in a dedicated decoder panel.
* **Read-only bitrate reporting** — queries the configured interface bitrate via netlink; bus configuration itself stays external (`ip link set ...`), no in-app privilege escalation.

**Shared console & tooling**

* **Console** — simultaneous HEX / ASCII / BINARY / DECIMAL columns, colour-coded by direction, batch-rendered at 60 Hz with a rolling history so high-baud streams stay responsive; CAN sessions render a `candump`-style frame header instead.
* **pcap capture & replay** — record traffic to a `LINKTYPE_RTAC_SERIAL` pcap that opens directly in Wireshark, and replay a capture back through the console offline with the original inter-packet timing.
* **Live statistics** — per-direction byte counts, throughput rates and chart, line utilisation, and inter-packet gap timing, plus dropped-byte accounting when a peer stops draining.
* **Byte inspector** — decodes exactly the bytes you select in the console into Int8–64 / Float / Double / Hex / Binary / ASCII, little- and big-endian.
* **Multi-session tabs** — run several serial and/or CAN sessions at once, each fully independent.
* **Robust backend** — non-blocking per-direction write queues (a stalled peer can't wedge the other direction), RAII-clean teardown, and no silent write loss.

## Installation

AetherBus is distributed for Linux through the Snap Store and GitHub Releases.

### Snap Store (Linux)

[![Get it from the Snap Store](https://snapcraft.io/static/images/badges/en/snap-store-black.svg)](https://snapcraft.io/aetherbus)

```bash
sudo snap install aetherbus
sudo snap connect aetherbus:serial-port   # grant access to /dev/tty* devices
sudo snap connect aetherbus:raw-usb       # grant access to USB-serial adapters
```

### GitHub Releases (Linux, macOS, Windows)

Pre-compiled packages are available on the [GitHub Releases Page](https://github.com/beratatmaca/AetherBus/releases):

* **Linux**: Download the `.deb` package (Debian/Ubuntu-based systems).
* **Windows**: Download the `.msi` installer.
* **macOS**: Download the `.dmg` package.

> **Platform notes on live interception:**
> - **Linux & macOS** allocate a real pseudo-terminal, so the reported slave path (e.g. `/dev/pts/5` or `/dev/ttys004`) behaves exactly like a serial port — point any target application at it directly.
> - **Windows** has no pseudo-terminal and no driver-free way to publish a real `COMx` device to third-party applications (that requires a signed kernel driver such as [com0com](https://com0com.sourceforge.net/)). AetherBus therefore opens and fully configures the physical COM port and exposes the application side as a **named pipe** (`\\.\pipe\aetherbus-*`). Capture, statistics, byte injection, and modem-line control all work; any client that can attach to the named pipe is proxied, but the virtual side is not automatically visible as a `COMx` port.

## The Console

Captured traffic streams into a colour-coded, monospaced viewport, rendered side-by-side as the selected format plus an ASCII gutter, exactly like a hardware terminal:

```text
[14:23:01.004 Tx]  41 42 43 0D 0A    |  ABC..
[14:23:01.012 Rx]  06 3F 21          |  .?!
```

A CAN session renders the same console with a `candump`-style header instead of raw framing:

```text
[14:23:01.004 Tx]  123   [8]  DE AD BE EF 00 11 22 33
[14:23:01.012 Rx]  7DF   [3]  02 01 00
```

Toggle the display between **HEX**, **ASCII**, **BINARY**, and **DECIMAL** at any time — the columns render side by side and can be combined. The view is batch-rendered at 60 Hz and capped to a rolling history so even 921600-baud streams stay responsive, with autoscroll, pause, timestamp toggle, and in-buffer search. Alongside the console, a stats panel shows live throughput, utilisation, and gap timing, and you can arm a **Capture…** to a pcap file or **Replay…** a previous capture back through the same view. `ThemeController` switches between `assets/theme-light.qss` and `assets/theme-dark.qss` (System / Light / Dark, selectable from the View menu), keeping both the stylesheet and the Qt palette in sync.

## Technical Architecture

The architecture separates the interception engine from the presentation layer: the backend (`aether_core`) compiles as a static library with **zero graphical dependencies** and is unit-tested in isolation. Serial and CAN are two independent transports behind a shared interface, so the GUI never depends on a concrete backend.

**Core — common (`src/core/common/`)**

* **`i_bus_backend`**: `IBusBackend`, the transport-neutral capture/lifecycle interface (`chunkCaptured` / `started` / `stopped` / `errorOccurred` / `disconnected`, `close()` / `isRunning()`) that both `PtyProxy` and `CanBackend` implement.
* **`format_codec`**: A pure, side-effect-free conversion layer (bytes ⇄ HEX / ASCII / BINARY / DECIMAL) and the injection-field parsers.
* **`stats_calculator`**: Throughput rates, line utilisation, and inter-packet gap statistics from the captured chunk stream, plus per-CAN-ID stats.
* **`pcap_writer`**: Shared, thread-safe writer for the `LINKTYPE_RTAC_SERIAL` capture format, used by every backend.
* **`capture_replay`**: Parses the `LINKTYPE_RTAC_SERIAL` pcap files the backend writes and replays them as `CapturedChunk`s with the original timing.
* **`signal_cleanup`**: Releases descriptors and symlinks on fatal signals so a crash can't leave `/dev/ttyUSB0` locked.

**Core — serial (`src/core/serial/`)**

* **`PtyProxy`**: The public, platform-neutral interception backend. It hands the GUI a single `open()` / `close()` / inject / capture / stats surface and dispatches to a per-platform implementation behind a pimpl, tagging UART bytes **Rx** and target-app bytes **Tx** over non-blocking, per-direction write queues so one stalled side can't wedge the other, mirroring line-setting changes onto the device, tracking byte/drop counters, and emitting every chunk to the GUI over thread-safe queued signals.
  * **`PosixPtyProxy`** (`serial/posix/`, base for Linux & macOS): opens the physical UART in raw mode via `termios`, allocates a master/slave pseudo-terminal pair (`posix_openpt` / `grantpt` / `unlockpt` / `ptsname`), and runs a background `poll()` multiplexing loop with RAII-clean teardown via a self-pipe wake and symlink unlink. `LinuxPtyProxy` (`serial/linux/`) and `MacPtyProxy` (`serial/mac/`) are thin subclasses that differ only in how a non-standard baud rate is applied.
  * **`WindowsPtyProxy`** (`serial/win/`): opens and configures the COM port (`SetCommState` / `SetCommTimeouts`), exposes the application side as a named pipe, and multiplexes overlapped I/O on a single worker via `WaitForMultipleObjects` — the same Rx/Tx tagging, capture, stats, injection, and modem-line control as the POSIX path.
* **`linux_baud` / `mac_baud`**: Arbitrary (non-standard) baud rates via the Linux `termios2` path and the macOS `IOSSIOSPEED` ioctl respectively, each isolated from `<termios.h>`.

**Core — CAN (`src/core/can/`, Linux/SocketCAN only; safe no-op stubs elsewhere)**

* **`CanBackend`**: `IBusBackend` implementation over `PF_CAN`/`SOCK_RAW` sockets — classic and CAN-FD frames, a `poll()` worker mirroring the serial proxy's loop, per-socket ID/mask filters, loopback/recv-own/error-frame options, interface discovery (`/sys/class/net`, no root/shell), and read-only bitrate querying via RTNL netlink (`RTM_GETLINK` → `IFLA_CAN_BITTIMING`).
* **`dbc_parser`**: Parses CAN DBC database files into signal definitions for the decoder panel.

**GUI (`src/gui/`)** — Qt 6 Widgets front end that only ever consumes `CapturedChunk` signals and never touches a raw descriptor:

* **`MainWindow`** hosts multiple session tabs behind the abstract **`SessionView`** (`gui/sessions/`), so serial and CAN sessions are managed polymorphically.
* **`SessionWidget`** (serial, `gui/sessions/`) owns one `PtyProxy` and composes **`ConfigPanel`** (device & line settings), **`SignalPanel`** (RTS/DTR/BREAK + modem-line LEDs), **`InjectionPanel`** (byte injection), **`StatsPanel`** + **`ThroughputChart`** (live metrics), and **`MacroBar`** (macros & history), all in `gui/panels/` / `gui/widgets/`.
* **`CanSessionWidget`** (CAN, `gui/sessions/`) owns one `CanBackend` and pairs **`CanConfigPanel`** (interface, filters, FD/loopback/recv-own/error toggles) with a cansend-style transmit bar, plus a **`CanSnifferWidget`** (per-ID table) and **`CanDecoderPanel`** (DBC-driven live signal values) alongside the shared log view.
* Both session types render through the same **`ConsoleView`** / **`ConsolePanel`** (rendering, search, byte selection) and **`ByteInspectorPanel`** (decodes the selected bytes into common data types), themed by **`ThemeController`**.

---

## Getting Started

### Prerequisites

To build AetherBus, you will need:

* **CMake** (v3.16 or higher)
* **Qt6 SDK** (the `Core`, `Widgets`, and `Network` modules; `Test` is also needed to build the test suite)
* **C++17 compliant compiler** (GCC 10+, Clang 12+, or MSVC 2019+)

### Build Instructions

To compile the project:

```bash
# Clone the repository
git clone https://github.com/beratatmaca/AetherBus.git
cd AetherBus

# Configure and compile (Release mode)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

To run the unit + integration test suite (includes a live pseudo-terminal loopback):

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### Developer Tooling

The build provides helper targets for formatting, linting, and documentation:

```bash
cmake --build build --target format        # apply clang-format in place
cmake --build build --target format-check  # verify formatting (CI-safe)
cmake --build build --target tidy          # run clang-tidy static analysis
cmake --build build --target docs          # generate Doxygen HTML
```

---

## Versioning

AetherBus uses an **auto-incrementing version** of the form `MAJOR.MINOR.PATCH.BUILD`:

* **`MAJOR.MINOR.PATCH`** — the semantic base, kept in the top-level [`VERSION`](VERSION) file. Bump it by hand for meaningful releases.
* **`BUILD`** — the git commit count (`git rev-list --count HEAD`), which increases automatically with every commit/merge to `main`, so each build gets a unique, monotonically increasing version with no manual edits.

The version is the single source of truth across the whole project:

| Surface                             | How it gets the version                              |
| ----------------------------------- | ---------------------------------------------------- |
| CMake (`PROJECT_VERSION`)           | `cmake/Version.cmake` resolves it before `project()` |
| The binary (`aether/version.h`)     | generated header (e.g. `AETHER_VERSION_STRING`)      |
| Packages (`.deb` / `.msi` / `.dmg`) | CPack uses the same full version in filenames        |
| Snap                                | `snapcraft.yaml` derives it in `override-pull`       |
| GitHub release page                 | the `version` job in `release.yml`                   |

CMake derives the build number from git automatically. CI passes the exact
values in so every matrix runner agrees:

```bash
cmake -B build -DAETHER_BUILD_NUMBER=42 -DAETHER_GIT_SHA=1a2b3c4
```

### Releases

The release pipeline ([`release.yml`](.github/workflows/release.yml)) computes the version once and shares it with every build job and the release page:

* **Push to `main`** → packages are built for Linux/macOS/Windows and published to the rolling **`latest`** pre-release, whose name and notes show the current incrementing version (e.g. *AetherBus v1.0.2.87 (latest main)*). The Snap package is built and published straight to the **`stable`** channel on every push, so `main` is expected to stay release-ready at all times.
* **Push a `vX.Y.Z` tag** → a normal (non-pre-release) GitHub Release named after the tag, and the same Snap build is published to `stable` under that version. To cut one, set `VERSION` to `X.Y.Z`, commit, then:

  ```bash
  git tag v1.1.0 && git push origin v1.1.0
  ```

---

## Usage Reference

Launch AetherBus and intercept a live serial link in five steps:

1. **Select the device.** Pick the physical port (e.g. `/dev/ttyUSB0`) from the scanned list and set the line parameters — baud rate, data bits, parity, and stop bits — to match the hardware.
2. **Start interception.** Click **Start Interception**. AetherBus opens the UART, spins up the proxy loop, and reports the kernel-assigned virtual port (e.g. `/dev/pts/5`) in the status bar. Optionally provide a stable symlink (e.g. `./ttyUSB0_sniffed`) so the target app always finds the same path.
3. **Point your application at the virtual port.** Connect your existing tool — `minicom`, a firmware flasher, or your own software — to the reported slave path instead of the real device. All traffic now flows through AetherBus and appears live in the console, colour-coded by direction.
4. **Decode and inject.** Switch the console format on the fly, and use the injection panel to send crafted bytes — as space-separated HEX (`41 42 0D 0A`), ASCII, decimal, or binary, with an optional CR/LF ending and one-shot or repeating delivery — directly to either the **device** or the **application** side of the link. Save frequently-used payloads as macros.
5. **Capture and replay.** Arm **Capture…** to record everything to a `LINKTYPE_RTAC_SERIAL` pcap (open it in Wireshark), or **Replay…** a previous capture back through the console for offline analysis with the original timing preserved.

### Example: sniffing a modem session with `minicom`

```bash
# Terminal 1 — start AetherBus, intercept /dev/ttyUSB0 at 115200 8N1.
# The status bar reports e.g. /dev/pts/5.

# Terminal 2 — point minicom at the virtual port instead of the hardware.
minicom -D /dev/pts/5
```

Every command minicom sends and every reply the modem returns is now mirrored, timestamped, and decoded in the AetherBus console.

### Example: sniffing a SocketCAN bus

```bash
# Bring up a virtual CAN interface for local testing (skip on real hardware).
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

In AetherBus: **File → New CAN Session**, pick `vcan0`, and **Start Capture**. Traffic appears in the log view immediately; switch to the **Sniffer View** tab for a live per-ID table, add ID/mask rules in the filter table to narrow what's shown, load a `.dbc` file in the decoder panel to see named signal values, and use the transmit bar (ID + EFF/RTR/FD/BRS + payload) to send frames directly — no arm step required.

---

## License

This project is open-source and available under the MIT License.

### Third-Party Software & LGPL Compliance

AetherBus links to the **Qt 6** framework, which is licensed under the **GNU Lesser General Public License (LGPL) v3**.

In compliance with the LGPL v3:

* In precompiled releases, the Qt libraries are linked dynamically.
* You can obtain the Qt source code at [qt.io/download](https://www.qt.io/download).
* You are permitted to modify the Qt libraries and relink this application with your modified version, in accordance with the terms of the LGPL v3.
