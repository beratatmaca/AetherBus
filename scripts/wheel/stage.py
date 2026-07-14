#!/usr/bin/env python3
"""Stage the compiled AetherBus binary and its Qt runtime into the wheel.

Populates ``python/src/aetherbus/_bundle/`` from an existing Release build so
that ``python -m build --wheel python`` produces a self-contained wheel:

* Linux   — ``bin/aetherbus`` (rpath rewritten to ``$ORIGIN/../lib``) +
            ``bin/qt.conf``, the Qt shared-library closure in ``lib/``, and
            the Qt plugin directories in ``plugins/``. System libraries
            (libxcb, libGL, glibc, libstdc++) are deliberately not vendored —
            same policy as PySide6.
* Windows — ``aetherbus.exe`` plus its vcpkg sibling DLLs, then windeployqt
            (Qt DLLs, plugins, VC++ runtime) next to it.
* macOS   — the whole ``aetherbus.app`` processed by macdeployqt.

The ``qoffscreen`` platform plugin is included on every OS so CI can smoke
test the wheel headlessly with QT_QPA_PLATFORM=offscreen.
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

# Plugin categories the app actually loads at runtime; wayland ones are
# copied only if the Qt install ships them.
LINUX_PLUGIN_DIRS = [
    "platforms",
    "platformthemes",
    "xcbglintegrations",
    "imageformats",
    "iconengines",
    "tls",
    "networkinformation",
    "wayland-decoration-client",
    "wayland-graphics-integration-client",
    "wayland-shell-integration",
]


def find_binary(build_dir: Path) -> Path:
    if sys.platform == "darwin":
        candidates = [build_dir / "aetherbus.app"]
    elif sys.platform == "win32":
        # Multi-config (Visual Studio) generators put the exe under Release/.
        candidates = [build_dir / "Release" / "aetherbus.exe", build_dir / "aetherbus.exe"]
    else:
        candidates = [build_dir / "aetherbus"]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit(f"stage.py: no built aetherbus found in {build_dir} (looked for {candidates})")


def qt_library_closure(elves):
    """Resolved paths of every Qt-owned shared library (libQt6*, libicu*)
    that any of the given ELF files (transitively) links, per ldd. Everything
    else in the closure is a system library and stays un-vendored."""
    closure = {}
    for elf in elves:
        output = subprocess.check_output(["ldd", str(elf)], text=True)
        for line in output.splitlines():
            if "=>" not in line:
                continue
            name, _, rest = line.strip().partition(" => ")
            target = rest.rsplit(" (", 1)[0].strip()
            if not target or target == "not found":
                continue
            if name.startswith(("libQt6", "libicu")):
                # Keep the SONAME-level file name ldd reported (libQt6Core.so.6),
                # dereferencing the symlink chain on copy.
                closure[Path(target).name] = Path(target).resolve()
    return closure


def stage_linux(build_dir: Path, qt_root: Path, dest: Path) -> None:
    bin_dir = dest / "bin"
    lib_dir = dest / "lib"
    plugins_dir = dest / "plugins"
    for directory in (bin_dir, lib_dir, plugins_dir):
        directory.mkdir(parents=True)

    binary = find_binary(build_dir)
    staged_binary = bin_dir / binary.name
    shutil.copy2(binary, staged_binary)

    # Copy plugin directories first: they are dlopen'd, so the binary's own
    # ldd closure misses their dependencies (libQt6XcbQpa etc.). ldd is run
    # against the plugins in their original location so their $ORIGIN rpaths
    # still resolve into the Qt tree.
    source_plugins = []
    for plugin_dir in LINUX_PLUGIN_DIRS:
        source = qt_root / "plugins" / plugin_dir
        if not source.is_dir():
            continue
        shutil.copytree(source, plugins_dir / plugin_dir)
        source_plugins.extend(source.glob("*.so"))

    for name, source in sorted(qt_library_closure([binary, *source_plugins]).items()):
        shutil.copy2(source, lib_dir / name)

    # Qt's own libs and plugins already carry $ORIGIN-relative rpaths that
    # match this lib/ + plugins/ layout; only our binary needs rewriting.
    subprocess.check_call(["patchelf", "--set-rpath", "$ORIGIN/../lib", str(staged_binary)])
    (bin_dir / "qt.conf").write_text("[Paths]\nPrefix = ..\nPlugins = plugins\nLibraries = lib\n")


def stage_windows(build_dir: Path, qt_root: Path, dest: Path) -> None:
    dest.mkdir(parents=True)
    binary = find_binary(build_dir)
    staged_binary = dest / binary.name
    shutil.copy2(binary, staged_binary)
    # vcpkg's applocal step drops non-Qt DLLs (pcap/packet) next to the exe;
    # windeployqt only handles Qt DLLs, so carry the siblings over explicitly.
    for dll in binary.parent.glob("*.dll"):
        shutil.copy2(dll, dest / dll.name)
    subprocess.check_call(
        ["windeployqt", "--release", "--compiler-runtime", "--no-translations", str(staged_binary)]
    )
    offscreen = qt_root / "plugins" / "platforms" / "qoffscreen.dll"
    if offscreen.is_file():
        shutil.copy2(offscreen, dest / "platforms" / offscreen.name)


def stage_macos(build_dir: Path, qt_root: Path, dest: Path) -> None:
    dest.mkdir(parents=True)
    app = find_binary(build_dir)
    staged_app = dest / app.name
    shutil.copytree(app, staged_app, symlinks=True)
    subprocess.check_call(["macdeployqt", str(staged_app)])
    offscreen = qt_root / "plugins" / "platforms" / "libqoffscreen.dylib"
    if offscreen.is_file():
        platforms = staged_app / "Contents" / "PlugIns" / "platforms"
        platforms.mkdir(parents=True, exist_ok=True)
        shutil.copy2(offscreen, platforms / offscreen.name)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True, help="CMake build directory")
    parser.add_argument("--qt-root", type=Path, required=True, help="Qt install prefix (QT_ROOT_DIR)")
    parser.add_argument("--dest", type=Path, required=True, help="target _bundle directory")
    args = parser.parse_args()

    if args.dest.exists():
        shutil.rmtree(args.dest)

    if sys.platform == "darwin":
        stage_macos(args.build_dir, args.qt_root, args.dest)
    elif sys.platform == "win32":
        stage_windows(args.build_dir, args.qt_root, args.dest)
    else:
        stage_linux(args.build_dir, args.qt_root, args.dest)

    # PyPI metadata references LICENSE.txt relative to the python/ project root.
    shutil.copy2(REPO_ROOT / "LICENSE.txt", REPO_ROOT / "python" / "LICENSE.txt")

    total = sum(f.stat().st_size for f in args.dest.rglob("*") if f.is_file())
    print(f"stage.py: staged {total / 1e6:.1f} MB into {args.dest}")


if __name__ == "__main__":
    main()
