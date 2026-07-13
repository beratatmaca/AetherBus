# Welcome to AetherBus

AetherBus is a unified utility for serial communication, SocketCAN analysis, and raw Ethernet network monitoring.

This tutorial walks through every session type, the workspace layouts, and how the app now remembers your setup between launches. Use **Next** / **Back** to move through it, or check **Don't show this again** once you're comfortable.

---

## Serial Sessions

**File ➔ New Serial Session** (or `Ctrl+N`) opens a serial interception session:

* **Interception, not just a terminal**: AetherBus opens the real device (e.g. `/dev/ttyUSB0`) and creates a pseudo-terminal (PTY) proxy. Your own application talks to the PTY's slave path (shown in the panel, or a custom symlink you set) while AetherBus mirrors every byte in both directions.
* **Connection settings**: device, baud rate, data bits, parity, stop bits, and flow control — press `F5` or the reload button to rescan `/dev` for ports. Enable **Direct Connection** to skip the PTY proxy and talk to the hardware yourself.
* **Console view**: toggle between ASCII, Hex, Binary, and Decimal at once, color-coded by direction (Rx/Tx). Use **Save Received** to dump the console to a text file, or **Toggle Logging** to stream everything to a file continuously as it arrives.
* **Inject & replay**: send raw bytes or whole files to either side of the proxy; capture traffic to a `.pcap` and replay it later, paced by the original timestamps.
* **Signals panel**: toggle RTS/DTR, send a BREAK, and watch CTS/DSR/DCD modem status live.
* **Macros**: save frequently sent payloads (any format, any line ending) as one-click macros.

---

## CAN Sessions

**File ➔ New CAN Session** opens a SocketCAN capture/transmit session, candump/cansend-style:

* **Interface & socket options**: pick or type an interface (`can0`, `vcan0`, …), enable CAN-FD (up to 64-byte payloads), local loopback, receiving your own transmitted frames, and error frames.
* **Receive filters**: add ID/mask rules (hex), mark them extended or inverted, and toggle each on/off without deleting it.
* **Transmit bar**: send a frame by ID + data with extended/RTR/FD/BRS flags; recent sends are kept in a recall history.
* **Macros**: save named frames (ID, payload, flags) for one-click re-transmission, same idea as the serial macro bar.
* **DBC decoding**: load a `.dbc` file (or define custom signals by hand) to decode raw frames into named, scaled signal values in real time.
* **Capture & replay**: log frames to a file and replay a previous capture later.

---

## Ethernet Sessions

**File ➔ New Ethernet Session** opens a raw packet capture/crafting session (requires libpcap):

* **Capture**: choose a network interface and an optional BPF filter (tcpdump syntax, e.g. `port 80` or `udp`), then **Start Capture**.
* **Wireshark-style inspection**: a scrolling packet list, a protocol detail tree, and a hex/ASCII dump — all three always visible and kept in sync as you select packets.
* **Packet Constructor**: craft ICMP Echo Requests, UDP, or TCP segments (with sequence/ack numbers and SYN/ACK/FIN/RST/PSH/URG flags) — IP and transport checksums are computed automatically before injection.
* **Quick-send macros**: save a constructed packet as a named macro, persisted across restarts.
* **Save / replay pcap**: save a capture to a standard `.pcap` file, or play one back onto the interface at its original pace.

---

## Workspace Layouts

All open sessions — Serial, CAN, and Ethernet alike — live in the same workspace and can be arranged dynamically:

* **Tabbed Mode (default)**: sessions stack as tabs with native close ("×") buttons.
* **Tiled Mode**: **Window ➔ Tile Workspace** arranges every open session into a balanced grid (side-by-side for 2, a 2×2 grid for 4, and so on) instead of one cramped row.
* **Reset Layout**: **Window ➔ Reset Layout** collapses any tiled panes back into tabs.
* Drag any splitter handle to resize panes — including the Ethernet session's packet list, protocol tree, and hex dump.

---

## Remembering Your Work

AetherBus remembers your whole workspace between launches — you don't have to rebuild it every time:

* Every open session (Serial, CAN, or Ethernet) reappears with its own saved settings — device/baud, CAN interface/flags/filters, Ethernet interface/BPF filter — exactly as you left them.
* Restored sessions come back **idle**: nothing reconnects to hardware or starts a capture automatically. Review the settings, then click **Start**/**Connect** yourself.
* The window's size, position, and whether you were in Tabbed or Tiled mode are restored too.
* Closing all sessions and relaunching for the first time still starts you with a single blank Serial session — persistence only kicks in once you've actually had something open.

---

## Tips & Shortcuts

* `Ctrl+N` — new Serial session. `Ctrl+W` — close the current session. `Ctrl+Q` — quit.
* `F5` (while a Serial or CAN config panel has focus) — rescan available ports/interfaces.
* **View ➔ Theme** — switch between System, Light, and Dark; the choice is remembered.
* Recently used serial ports are kept in the device dropdown so you don't have to retype them.
* **Help ➔ Welcome Tutorial…** reopens this guide at any time.
