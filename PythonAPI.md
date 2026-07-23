# Python Control API

AetherBus can expose a **local control channel** while it runs, so a Python
script can enumerate open sessions, create/configure/start/stop them, inject
data, stream captured traffic, capture to pcap, replay, run macros, and schedule
repeating sends — enough to automate a device test with no clicks.

The channel is a local owner-only socket (a Unix domain socket on Linux/macOS, a
named pipe on Windows). There is **no network exposure**; only processes running
as the same user can connect.

> The reference below is generated from the `aetherbus` client source, so it
> always matches the installed package.

---

## Enabling the channel

It is **off by default**. Turn it on one of two ways:

- launch with `aetherbus --control`, or
- toggle **Window → Enable Control Channel** (remembered across restarts).

`aetherbus --no-control` forces it off even if the saved toggle would enable it.

Install the client from PyPI:

```
pip install aetherbus
```

Then:

```python
import aetherbus

with aetherbus.connect() as c:
    sid = c.open_session(
        "serial",
        {"device": "/dev/ttyUSB0", "baud": 115200},
        start=True,
    )
    c.capture(sid, "start", "/tmp/run.pcap")
    c.send(sid, b"PING\r\n")
    print(c.stats(sid))
    c.close_session(sid)
```

Config keys by session type (omit a key to keep its current value):

- **serial** — `device`, `baud`, `dataBits`, `parity`
  (`none`/`even`/`odd`), `stopBits`, `flow`
  (`none`/`rtscts`/`xonxoff`), `symlinkPath`, `directMode`
- **can** — `iface`, `fdMode`, `loopback`, `receiveOwn`,
  `errorFrames`, `filters` (list of
  `{"id", "mask", "extended", "invert"}`)
- **ethernet** — `interface`, `bpfFilter`

Stream a session's captured traffic (use a separate connection if you also send
commands, so a traffic burst never blocks a command reply):

```python
stream = aetherbus.connect()
for msg in stream.stream(sid):
    print(msg["dir"], msg["data"])   # dir is "rx"/"tx", data is bytes
```

---

## Module functions

### connect

`connect(path=None) -> Client`

Connect to a running AetherBus GUI's control channel.

### default_socket_path

`default_socket_path() -> str`

Mirror ControlServer::socketName() so the client finds the GUI's socket.

## Client methods

A connection to a running AetherBus control channel.

### sessions

`sessions() -> list[dict]`

Return the open sessions: ``[{"id", "type", "name", "running"}, ...]``.

### send

`send(session, data, *, side='device', frame_id=None, flags=0) -> None`

Send bytes/a frame into ``session``.

Serial: ``side`` is ``"device"`` (toward the UART) or ``"app"`` (toward
the target). CAN: pass ``frame_id`` (and optionally ``flags``). Ethernet:
``data`` is a full raw frame.

### open_session

`open_session(type, config=None, *, start=False) -> int`

Create a new session and return its id.

``type`` is ``"serial"``, ``"can"`` or ``"ethernet"``. ``config`` is an
optional dict applied to the new session before it starts:

- serial: ``device, baud, dataBits, parity, stopBits, flow, symlinkPath, directMode``
- can: ``iface, fdMode, loopback, receiveOwn, errorFrames, filters``
  (``filters`` is a list of ``{"id", "mask", "extended", "invert"}``)
- ethernet: ``interface, bpfFilter``

With ``start=True`` the session's backend is started immediately.

### close_session

`close_session(session) -> None`

Close (and destroy) ``session``.

### start

`start(session, config=None) -> None`

Start ``session``'s backend, optionally applying ``config`` first.

See `open_session` for the per-type `config` keys.

### stop

`stop(session) -> None`

Stop ``session``'s backend (leaves the tab open).

### stats

`stats(session) -> dict`

Return ``session``'s counters: ``{rxBytes, txBytes, rxChunks, txChunks, rxRate, txRate, running}``.

### capture

`capture(session, action, path=None) -> bool`

Control ``session``'s pcap capture.

``action`` is ``"start"`` (needs ``path``), ``"stop"`` or ``"status"``.
Returns whether a capture is now active.

### replay

`replay(session, path, action='start') -> None`

Replay a saved pcap through ``session`` (offline view). ``action`` is ``"start"`` or ``"stop"``.

### run_macro

`run_macro(session, name=None, index=None) -> int`

Fire one of ``session``'s quick-send macros by ``name`` or ``index``; returns the index fired.

### schedule_send

`schedule_send(session, data, interval_ms, *, count=None, side='device', frame_id=None, flags=0) -> int`

Repeat a send every ``interval_ms`` ms; returns the schedule id.

Without `count` it repeats until `cancel_send` (or the session
closes). `side`/`frame_id`/`flags` mirror `send`.

### cancel_send

`cancel_send(schedule) -> None`

Cancel a repeating send created by `schedule_send`.

### stream

`stream(session) -> Iterator[dict]`

Subscribe to ``session`` and yield one dict per captured chunk.

Each yielded dict is ``{"dir", "ts", "data": bytes, ...}`` (CAN adds
``frameId``/``flags``). If the GUI sheds messages under backpressure a
``{"event": "dropped", "count": N}`` dict is yielded. Runs until the
connection closes or the generator is closed. Use a dedicated client
for streaming if you also issue commands concurrently.

### close

`close() -> None`

_(undocumented)_


---

## Protocol notes

The wire format is newline-delimited JSON, one object per line; binary payloads
are lowercase hex strings. Each command is tagged with an `id` echoed in its
reply, so replies correlate even when async traffic events interleave. The
current protocol version is reported in the `hello` greeting (`"protocol": 2`);
new verbs are additive, so older clients keep working. Any error is raised
client-side as `aetherbus.ControlError`.
