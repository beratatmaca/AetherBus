# Changelog

All notable changes to AetherBus are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to a `MAJOR.MINOR.PATCH.BUILD` version scheme (see the
[Versioning](README.md#versioning) section of the README). The `BUILD` component
is derived automatically from the git commit count and is omitted here; entries
are grouped by the semantic `MAJOR.MINOR.PATCH` base recorded in [`VERSION`](VERSION).

## [1.0.2]

### Changed

- Refactored split/newline mode selection with 6 distinct modes (CR, LF, CR+LF, every packet, every N bytes, header byte array) and new format selector for header byte array detection (HEX, ASCII, DEC, BIN). Removed TLV mode and simplified naming.

## [1.0.1]

### Added

- Mirror slave-PTY line-setting changes onto the physical UART: when the target
  application reconfigures baud/parity/framing on the virtual port, the change is
  now applied to the real device and reported in the UI.
- Independent backend byte counters (Rx / Tx / dropped), accurate even if the UI
  stalls, surfaced in the console toolbar.
- Record captured traffic to a pcap file (`LINKTYPE_RTAC_SERIAL`) that opens
  directly in Wireshark, written straight from the capture thread.

### Changed

- Rewrote the proxy write path around non-blocking, per-direction queues so a
  stalled peer on one side can no longer wedge traffic in the other direction.
- Injection failures are now surfaced on every send path (panel, macro bar, and
  file send) with a specific message distinguishing "interception not started"
  from "send buffer full — bytes dropped".

### Removed

- Dropped the unused `network` and `network-bind` snap plugs; AetherBus is a
  local serial tool and requests no network access.

### Fixed

- Write failures (broken pipe / disconnected device) and dropped bytes under
  backpressure are no longer silently ignored — they raise a disconnect or a
  throttled warning.

## [1.0.0] - 2026-07-01

### Added

- Initial release: transparent serial-port interceptor with a PTY man-in-the-middle
  proxy and an HTerm-style diagnostic console.
- Side-by-side HEX / ASCII / BINARY / DECIMAL views, colour-coded by direction and
  batch-rendered at 60 Hz for high-baud streams.
- Live byte injection to either the device or the application side of the link.
- Configurable line settings (baud, data bits, parity, stop bits, flow control),
  including non-standard baud rates via the Linux `termios2` path.
- Linux `.deb`, Windows `.msi`, macOS `.dmg`, and Snap Store packaging.

[1.0.0]: https://github.com/beratatmaca/AetherBus/releases/tag/v1.0.0
