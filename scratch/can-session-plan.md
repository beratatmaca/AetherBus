# Dry-Run Plan: CAN Bus Session Support (SocketCAN)

**Status:** AWAITING USER SIGN-OFF — no production edits until approved.
**Date:** 2026-07-01
**Requested by:** user

## 1. Goal & confirmed decisions

Add CAN bus receive/transmit ("candump"/"cansend" equivalent) to AetherBus,
reusing the existing display, stats, logging, and injection layers.

Confirmed with user:
- **CAN is a separate session type** (own tab kind), NOT a mode toggle inside the
  serial config panel.
- **Bitrate is read-only**: the interface (e.g. `can0`) is pre-configured
  externally (`ip link set can0 type can bitrate ...`); the GUI opens the socket
  and *displays* the current bitrate/state. No privilege escalation in-app.
- **View = candump-style scrolling log**, reusing `ConsoleView`.
- **Scope = classic CAN + CAN-FD** (`can_frame` + `canfd_frame`, up to 64 B, BRS/ESI).
- **Direct Transmit button** — no arm/confirm step before frames hit the bus.
- Platform: Linux SocketCAN. mac/Windows keep serial-only (CAN type hidden/stubbed).
- Test against virtual `vcan0` (no hardware / no root needed).

## 2. Loose-coupling principle (per CLAUDE.md)

Views speak two abstractions only — `CapturedChunk` (data) and `IBusBackend`
(lifecycle/capture). No view/widget ever includes `<linux/can.h>` or touches a
socket. SocketCAN lives entirely in `core/linux/linux_can_backend.*`.

## 3. Data model changes

### 3.1 Extend `core/serial_types.h` — optional frame header on `CapturedChunk`
Add fields that the serial path leaves default (zero) and CAN populates:
```cpp
struct CapturedChunk {
    qint64 timestampMs = 0;
    Direction dir = Direction::Rx;
    QByteArray data;              // payload bytes (unchanged)
    // --- optional framing metadata (CAN); serial leaves defaults ---
    bool     isFrame  = false;    // true => frameId/frameFlags valid
    quint32  frameId  = 0;        // CAN id (11 or 29 bit, no flag bits)
    quint16  frameFlags = 0;      // bitmask: EFF/RTR/ERR/FD/BRS/ESI (see FrameFlag)
};
enum FrameFlag : quint16 { Eff=1, Rtr=2, Err=4, Fd=8, Brs=16, Esi=32 };
```
Rationale: whole existing pipeline (queued signal, console ring buffer, logging,
gap-timing) keeps working; lowest churn; avoids touching every consumer that a
`std::variant` event would force.

### 3.2 New `core/can_types.h`
```cpp
struct CanFilter { quint32 id = 0; quint32 mask = 0; bool invert = false; };
struct CanConfig {
    QString iface;                 // "can0" / "vcan0"
    QVector<CanFilter> filters;    // empty => accept all
    bool listenOnly = false;       // (display only; set externally via ip link)
    bool loopback = true;          // CAN_RAW_LOOPBACK
    bool recvOwn = false;          // CAN_RAW_RECV_OWN_MSGS
    bool fdMode = true;            // CAN_RAW_FD_FRAMES
    bool errorFrames = true;       // CAN_RAW_ERR_FILTER (all)
    [[nodiscard]] QString validate() const;   // iface non-empty, payload sizes etc.
};
```
`SerialConfig` is untouched.

## 4. Backend abstraction

### 4.1 New `core/i_bus_backend.h` — minimal common interface
```cpp
class IBusBackend : public QObject {
    Q_OBJECT
public:
    ~IBusBackend() override = default;
    virtual bool close() = 0;                 // idempotent stop
    [[nodiscard]] virtual bool isRunning() const = 0;
signals:
    void chunkCaptured(const aether::CapturedChunk &chunk);
    void started(const QString &info);        // serial: slave path; CAN: "can0 @ 500k"
    void stopped();
    void errorOccurred(const QString &message);
    void disconnected();
};
```
Notes:
- `open(...)` stays type-specific (`SerialConfig` vs `CanConfig`) — kept off the
  interface to avoid a leaky union. `SessionWidget` knows its own type so it can
  call the concrete `open`.
- Serial-only extras (`setRts/setDtr/sendBreak/injectToApp/modemLines/
  lineReconfigured/writeStalled`) stay on `PtyProxy` only, NOT on the interface.
- `PtyProxy` gains `IBusBackend` as a base (its existing signals already match;
  `started(QString)` already carries the slave path). Low-risk.

### 4.2 New `core/linux/linux_can_backend.{h,cpp}` — `CanBackend : IBusBackend`
- `bool open(const CanConfig&)`:
  - `socket(PF_CAN, SOCK_RAW, CAN_RAW)`.
  - resolve ifindex via `ioctl(SIOCGIFINDEX)`; `bind()` `sockaddr_can`.
  - `setsockopt` `CAN_RAW_FD_FRAMES` (if fdMode), `CAN_RAW_LOOPBACK`,
    `CAN_RAW_RECV_OWN_MSGS`, `CAN_RAW_FILTER` (from `filters`),
    `CAN_RAW_ERR_FILTER`.
  - Worker thread runs a `poll()` loop on {socket fd, shutdown eventfd}, same
    pattern as `linux_pty_proxy`. On readable: `recv` into `canfd_frame`; decode
    id/flags/len; emit `chunkCaptured` (dir = Rx; Tx only for loopback echoes of
    our own sends when recvOwn/loopback surface them).
  - Emits `started("can0 @ <bitrate> (<state>)")` after reading bitrate via
    netlink (RTNL, unprivileged) — helper `queryCanBitrate(iface)`.
- `bool sendFrame(quint32 id, quint16 flags, const QByteArray &payload)`:
  builds `can_frame`/`canfd_frame`, `write()`s to the socket. On success emits a
  Tx `chunkCaptured` so the console shows what we transmitted.
- Robust cleanup (close fd, join thread) in `close()`/dtor — mirrors PtyProxy.
- Enumeration helper `listCanInterfaces()`: scan `/sys/class/net/*/type` for
  value `280` (ARPHRD_CAN). Read-only, no shell, no root.

### 4.3 CMake
- `option(AETHER_ENABLE_CAN "SocketCAN backend" ON)` (Linux only; auto-OFF on
  APPLE/WIN32).
- Add `linux_can_backend.{cpp,h}` to `AETHER_CORE_SOURCES` under the Linux branch
  guarded by the option; `target_compile_definitions(aether_core PUBLIC
  AETHER_ENABLE_CAN)` when on. No new external deps (uses `<linux/can.h>`,
  `<linux/can/raw.h>` from kernel headers).

## 5. GUI changes

### 5.1 Session type plumbing
- `enum class SessionType { Serial, Can };` (in a small `gui/session_types.h` or
  in `session_widget.h`).
- `SessionWidget` ctor gains `SessionType type` (default `Serial` for back-compat).
  It stores `m_type`, instantiates the right backend behind `IBusBackend* m_backend`,
  and connects ONLY common signals via the interface. Serial-only connects
  (modem/RTS/DTR/break/lineReconfigured/writeStalled) guarded by
  `if (m_type == Serial)` using the concrete `PtyProxy*` it also keeps.
- `MainWindow`: replace single "New Session" with a small menu / split-button:
  **"New Serial Session"** and **"New CAN Session"** (and the `+` corner button
  offers both). Each calls `addNewSession(SessionType)`.

### 5.2 Config panel (CAN variant)
- Add `CanConfigPanel : QGroupBox` (sibling of `ConfigPanel`) OR give `ConfigPanel`
  a constructor-selected layout. **Chosen: separate `CanConfigPanel`** (keeps each
  panel simple; matches "separate session type"). Fields:
  - Interface combo (populated from `listCanInterfaces()`), Rescan button.
  - Read-only bitrate/state label (e.g. `can0 @ 500000 · up`).
  - CAN-FD checkbox, loopback / recv-own checkboxes, error-frames checkbox.
  - Filter editor: small table of {id (hex), mask (hex), invert}. Empty = all.
  - Start/Stop button.
  - Signal: `startCan(const CanConfig&)`, `stopRequested()`, `rescanRequested()`.

### 5.3 Injection panel (CAN transmit)
- Give `InjectionPanel` a `Mode {Serial, Can}` set at construction.
- CAN mode UI: ID field (hex), EFF/RTR/FD/BRS checkboxes, payload field (hex),
  single **Transmit** button (no toDevice/toApp split, no arm step — per user).
  Accept `cansend`-style `123#DEADBEEF` / `12345678#R` shorthand in the ID field
  as a convenience (parsed to id+flags+payload).
  - New signal: `transmitCanFrame(quint32 id, quint16 flags, const QByteArray &payload)`.
  - Repeat-timer reused as-is.
- `SessionWidget` (CAN type) routes `transmitCanFrame` -> `CanBackend::sendFrame`.

### 5.4 ConsoleView — frame-aware prefix
- Add `NewlineMode::Frame` (one line per chunk, but render CAN header).
- When `chunk.isFrame`, build the line prefix as candump-style:
  `123 [4]` (11-bit) or `1FFFFFFF [8]` (29-bit EFF, 8 hex), `R` marker for RTR,
  `F`/`B` markers for FD/BRS, error frames flagged/red. Payload bytes continue
  through existing `FormatCodec` hex/ascii columns (already variable-length, so
  64-B FD payloads work). Serial rendering path unchanged.
- Small helper on `DisplayLine`/build path to carry id/flags for prefixing.

### 5.5 Stats (CAN)
- `StatsCalculator`: add an optional per-ID map `{id -> {count, lastTs, gap, len}}`
  updated inside `addChunk` when `chunk.isFrame`. Existing byte/rate/gap logic
  reused unchanged.
- Add `setCanConfig(int bitrate)` + `canBusLoad()` (bits-on-wire / bitrate)
  parallel to `*BaudUtilization()`; `StatsPanel` shows "Bus load %" label variant.
  (Per-ID table view deferred to a later phase — log view is the first cut.)
- Serial sessions keep `setSerialConfig`; CAN sessions call `setCanConfig`.

## 6. Phased implementation order (each phase builds + is testable)

- **Phase A — core, no GUI:** serial_types extension, can_types.h,
  i_bus_backend.h, PtyProxy adopts interface, CanBackend + CMake option.
  Verify: builds on Linux; `CanBackend` opens `vcan0`, captures a frame injected
  via `cansend vcan0 123#DEADBEEF` (manual + unit test).
- **Phase B — session plumbing:** SessionType, MainWindow menu, SessionWidget
  backend-agnostic wiring. Verify: can open a CAN session tab, frames appear in
  the console with candump-style prefix.
- **Phase C — transmit:** InjectionPanel CAN mode + routing. Verify: Transmit
  button puts a frame on `vcan0` (observed by external `candump vcan0`).
- **Phase D — config + stats polish:** CanConfigPanel filters/flags, bitrate
  display, per-ID stats + bus load.

## 7. Tests (`tests/`)
- New `tests/bus_test_can.cpp`: guarded to skip if `vcan0` unavailable
  (`QSKIP`). Covers: config validate, filter round-trip, frame encode/decode
  (classic + FD), Rx capture via loopback, Tx send. Add to CMake `bus_test`.
- Reuse existing GUI test harness for a smoke test of the CAN session widget.

## 8. Verification checklist (CLAUDE.md phases)
- `cmake --build build --target format` then `format-check`.
- `cmake --build build --target tidy`.
- `ctest --test-dir build --output-on-failure`.
- Sanitizer build (address+undefined) for the new backend.
- Manual: `vcan0` round-trip candump/cansend parity.

## 9. Mandatory bookkeeping (CLAUDE.md)
- **VERSION:** 1.0.2 -> **1.1.0** (MINOR — user explicitly requested a feature).
- **CHANGELOG.md:** currently deleted in working tree. NEED DECISION: recreate it
  with `## [Unreleased] / ### Added - CAN bus session ...` (rule mandates an
  entry) or leave deleted?
- **HANDOVER.md:** create/update on completion documenting the CAN work + repo
  status + follow-ups (per-ID table view, netlink bitrate-set option, mac/win).

## 10. Open questions — RESOLVED
1. CHANGELOG.md: **SKIP** (user). VERSION bump also skipped unless user asks for 1.1.0.
2. Filters: **full id/mask filter editor lives in the left CAN config panel**, v1.
3. Stats: **measured from observed traffic only** — frame/byte rates + per-ID
   counts from captured frames. NO theoretical bitrate-utilization formula
   (`canBusLoad()` dropped). Read bitrate is display-only.
```
