"""Control-channel client for a running AetherBus GUI.

AetherBus opens a localhost control socket while it runs (a Unix domain socket
on Linux/macOS, a named pipe on Windows). This module speaks its
newline-delimited-JSON protocol using only the standard library, so scripts can
enumerate the open sessions, send data/frames into them, and stream captured
traffic back out.

Example::

    import aetherbus
    c = aetherbus.connect()
    for s in c.sessions():
        print(s["id"], s["type"], s["name"], s["running"])

    c.send(1, b"\\x41\\x42")                      # serial -> device side
    c.send(2, b"\\xde\\xad", frame_id=0x123)       # CAN frame
    for msg in c.stream(1):                        # live rx/tx of session 1
        print(msg["dir"], msg["data"])
"""

from __future__ import annotations

import json
import os
import socket
import sys
from typing import Iterator, Optional


def default_socket_path() -> str:
    """Mirror ControlServer::socketName() so the client finds the GUI's socket."""
    if sys.platform.startswith("win"):
        return r"\\.\pipe\aetherbus-control"
    base = os.environ.get("XDG_RUNTIME_DIR") or "/tmp"
    return f"{base}/aetherbus-{os.getuid()}.sock"


class ControlError(RuntimeError):
    """Raised when the GUI replies to a command with ``ok: false``."""


class Client:
    """A connection to a running AetherBus control channel."""

    def __init__(self, path: Optional[str] = None):
        self._path = path or default_socket_path()
        self._next_id = 0
        if sys.platform.startswith("win"):
            # Named pipes present a file-like interface on Windows.
            self._pipe = open(self._path, "r+b", buffering=0)
            self._sock = None
            self._rfile = self._pipe
        else:
            self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self._sock.connect(self._path)
            self._pipe = None
            self._rfile = self._sock.makefile("rb")
        # The server greets with a hello event on connect: {"event":"hello",
        # "protocol":N,"version":"..."}. Stash it for compatibility checks.
        self.hello = self._read()

    # -- low-level framing ------------------------------------------------
    def _write(self, obj: dict) -> None:
        line = (json.dumps(obj) + "\n").encode("utf-8")
        if self._sock is not None:
            self._sock.sendall(line)
        else:
            self._pipe.write(line)
            self._pipe.flush()

    def _read(self) -> dict:
        line = self._rfile.readline()
        if not line:
            raise ConnectionError("AetherBus control channel closed")
        return json.loads(line)

    def _command(self, obj: dict) -> dict:
        """Send a command (tagged with a request id) and return its reply.

        Async event lines (``{"event": ...}``) may interleave on the socket once
        subscribed; they're skipped here, and the reply is matched by echoed
        ``id`` so a command never mistakes a traffic event for its result.
        """
        self._next_id += 1
        rid = self._next_id
        obj = {**obj, "id": rid}
        self._write(obj)
        while True:
            msg = self._read()
            if "event" in msg:
                continue  # async (hello/chunks/dropped) — not our reply
            if msg.get("id") == rid:
                if not msg.get("ok", False):
                    raise ControlError(msg.get("error", "command failed"))
                return msg
            # A reply for a different id (shouldn't happen with synchronous use)
            # — ignore and keep looking.

    # -- public API -------------------------------------------------------
    def sessions(self) -> list[dict]:
        """Return the open sessions: ``[{"id", "type", "name", "running"}, ...]``."""
        return self._command({"cmd": "list"}).get("sessions", [])

    def send(
        self,
        session: int,
        data: bytes,
        *,
        side: str = "device",
        frame_id: Optional[int] = None,
        flags: int = 0,
    ) -> None:
        """Send bytes/a frame into ``session``.

        Serial: ``side`` is ``"device"`` (toward the UART) or ``"app"`` (toward
        the target). CAN: pass ``frame_id`` (and optionally ``flags``). Ethernet:
        ``data`` is a full raw frame.
        """
        cmd = {"cmd": "send", "session": session, "data": data.hex()}
        if frame_id is not None:
            cmd["frameId"] = int(frame_id)
            cmd["flags"] = int(flags)
        else:
            cmd["side"] = side
        self._command(cmd)

    def open_session(
        self,
        type: str,
        config: Optional[dict] = None,
        *,
        start: bool = False,
    ) -> int:
        """Create a new session and return its id.

        ``type`` is ``"serial"``, ``"can"`` or ``"ethernet"``. ``config`` is an
        optional dict applied to the new session before it starts:

        - serial: ``device, baud, dataBits, parity, stopBits, flow, symlinkPath, directMode``
        - can: ``iface, fdMode, loopback, receiveOwn, errorFrames, filters``
          (``filters`` is a list of ``{"id", "mask", "extended", "invert"}``)
        - ethernet: ``interface, bpfFilter``

        With ``start=True`` the session's backend is started immediately.
        """
        cmd = {"cmd": "open", "type": type, "start": bool(start)}
        if config:
            cmd["config"] = config
        return int(self._command(cmd)["session"])

    def close_session(self, session: int) -> None:
        """Close (and destroy) ``session``."""
        self._command({"cmd": "close", "session": session})

    def start(self, session: int, config: Optional[dict] = None) -> None:
        """Start ``session``'s backend, optionally applying ``config`` first.

        See `open_session` for the per-type `config` keys.
        """
        cmd = {"cmd": "start", "session": session}
        if config:
            cmd["config"] = config
        self._command(cmd)

    def stop(self, session: int) -> None:
        """Stop ``session``'s backend (leaves the tab open)."""
        self._command({"cmd": "stop", "session": session})

    def stats(self, session: int) -> dict:
        """Return ``session``'s counters: ``{rxBytes, txBytes, rxChunks, txChunks, rxRate, txRate, running}``."""
        return self._command({"cmd": "stats", "session": session}).get("stats", {})

    def capture(self, session: int, action: str, path: Optional[str] = None) -> bool:
        """Control ``session``'s pcap capture.

        ``action`` is ``"start"`` (needs ``path``), ``"stop"`` or ``"status"``.
        Returns whether a capture is now active.
        """
        cmd = {"cmd": "capture", "session": session, "action": action}
        if path is not None:
            cmd["path"] = path
        return bool(self._command(cmd).get("capturing", False))

    def replay(self, session: int, path: str, action: str = "start") -> None:
        """Replay a saved pcap through ``session`` (offline view). ``action`` is ``"start"`` or ``"stop"``."""
        cmd = {"cmd": "replay", "session": session, "action": action}
        if path is not None:
            cmd["path"] = path
        self._command(cmd)

    def run_macro(
        self,
        session: int,
        name: Optional[str] = None,
        index: Optional[int] = None,
    ) -> int:
        """Fire one of ``session``'s quick-send macros by ``name`` or ``index``; returns the index fired."""
        if name is None and index is None:
            raise ValueError("run_macro needs a name or an index")
        cmd = {"cmd": "run_macro", "session": session}
        if index is not None:
            cmd["index"] = int(index)
        else:
            cmd["name"] = name
        return int(self._command(cmd)["index"])

    def schedule_send(
        self,
        session: int,
        data: bytes,
        interval_ms: int,
        *,
        count: Optional[int] = None,
        side: str = "device",
        frame_id: Optional[int] = None,
        flags: int = 0,
    ) -> int:
        """Repeat a send every ``interval_ms`` ms; returns the schedule id.

        Without `count` it repeats until `cancel_send` (or the session
        closes). `side`/`frame_id`/`flags` mirror `send`.
        """
        cmd = {
            "cmd": "schedule_send",
            "session": session,
            "data": data.hex(),
            "interval_ms": int(interval_ms),
        }
        if count is not None:
            cmd["count"] = int(count)
        if frame_id is not None:
            cmd["frameId"] = int(frame_id)
            cmd["flags"] = int(flags)
        else:
            cmd["side"] = side
        return int(self._command(cmd)["schedule"])

    def cancel_send(self, schedule: int) -> None:
        """Cancel a repeating send created by `schedule_send`."""
        self._command({"cmd": "cancel_send", "schedule": schedule})

    def stream(self, session: int) -> Iterator[dict]:
        """Subscribe to ``session`` and yield one dict per captured chunk.

        Each yielded dict is ``{"dir", "ts", "data": bytes, ...}`` (CAN adds
        ``frameId``/``flags``). If the GUI sheds messages under backpressure a
        ``{"event": "dropped", "count": N}`` dict is yielded. Runs until the
        connection closes or the generator is closed. Use a dedicated client
        for streaming if you also issue commands concurrently.
        """
        self._command({"cmd": "subscribe", "session": session})
        try:
            while True:
                msg = self._read()
                event = msg.get("event")
                if event == "chunks":
                    for chunk in msg.get("chunks", []):
                        if isinstance(chunk.get("data"), str):
                            chunk["data"] = bytes.fromhex(chunk["data"])
                        yield chunk
                elif event == "dropped":
                    yield msg
                # else: hello or a stray command reply — ignore.
        finally:
            try:
                self._command({"cmd": "unsubscribe", "session": session})
            except (OSError, ControlError):
                pass  # connection already gone; nothing to unsubscribe

    def close(self) -> None:
        if self._sock is not None:
            self._sock.close()
        if self._pipe is not None:
            self._pipe.close()

    def __enter__(self) -> "Client":
        return self

    def __exit__(self, *exc) -> None:
        self.close()


def connect(path: Optional[str] = None) -> Client:
    """Connect to a running AetherBus GUI's control channel."""
    return Client(path)
