"""Locate and run the bundled AetherBus binary."""

import os
import sys
from pathlib import Path

_BUNDLE = Path(__file__).resolve().parent / "_bundle"


def binary_path() -> Path:
    """Path of the bundled platform binary inside the installed wheel."""
    if sys.platform == "darwin":
        return _BUNDLE / "aetherbus.app" / "Contents" / "MacOS" / "aetherbus"
    if os.name == "nt":
        return _BUNDLE / "aetherbus.exe"
    return _BUNDLE / "bin" / "aetherbus"


def main() -> int:
    argv = sys.argv[1:]
    if "--version" in argv:
        # Answered here rather than by the binary so the check works with no
        # display server and no Qt runtime involved at all.
        from importlib.metadata import version

        print(f"aetherbus {version('aetherbus')}")
        return 0

    exe = binary_path()
    if not exe.is_file():
        print(f"aetherbus: bundled binary missing at {exe}", file=sys.stderr)
        return 1

    env = os.environ.copy()
    if sys.platform.startswith("linux"):
        # qt.conf next to the binary is the primary plugin-lookup mechanism;
        # this is a fallback for tools that spawn the binary from elsewhere.
        env.setdefault("QT_PLUGIN_PATH", str(_BUNDLE / "plugins"))

    if os.name == "nt":
        import subprocess

        return subprocess.run([str(exe), *argv], env=env).returncode

    os.execve(str(exe), [str(exe), *argv], env)
    return 1  # unreachable; execve does not return on success
