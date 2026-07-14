#!/usr/bin/env python3
"""Smoke test an installed aetherbus wheel by launching the real binary.

Run inside a venv where the wheel is installed. Launches the bundled Qt
application directly (offscreen platform, so it works on headless CI),
verifies it is still alive after a few seconds — i.e. the binary started and
the bundled Qt runtime resolved — then shuts it down.
"""

import os
import subprocess
import sys
import time

ALIVE_SECONDS = 8


def main() -> int:
    from aetherbus._launcher import binary_path

    exe = binary_path()
    if not exe.is_file():
        print(f"smoke_run.py: bundled binary missing at {exe}", file=sys.stderr)
        return 1

    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")

    process = subprocess.Popen([str(exe)], env=env)
    time.sleep(ALIVE_SECONDS)
    if process.poll() is not None:
        print(
            f"smoke_run.py: binary exited within {ALIVE_SECONDS}s "
            f"(returncode {process.returncode})",
            file=sys.stderr,
        )
        return 1

    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    print(f"smoke_run.py: {exe.name} ran for {ALIVE_SECONDS}s and shut down cleanly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
