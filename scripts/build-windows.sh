#!/usr/bin/env bash
#
# Cross-compile AetherBus for 64-bit Windows using MinGW-w64 + Qt 6 in Docker.
# Produces a self-contained dist/windows/ directory (aetherbus.exe + all runtime
# DLLs + the Qt platform plugin) that runs on Windows with no Qt install.
#
# Usage:
#   scripts/build-windows.sh            # build (reuses the Docker image if present)
#   scripts/build-windows.sh --rebuild  # force-rebuild the Docker image first
#
# Requires: Docker with network access (to build the fedora:40 + MinGW-Qt6 image).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="aetherbus-mingw"
OUT="$REPO/dist/windows"

if [[ "${1:-}" == "--rebuild" ]] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo ">> Building Docker image '$IMAGE' (MinGW-w64 + Qt 6)…"
    docker build -t "$IMAGE" -f "$REPO/docker/Dockerfile.mingw" "$REPO"
fi

echo ">> Cross-compiling and bundling…"
rm -rf "$OUT"
mkdir -p "$OUT"

# The repo is mounted read-only; the build happens on a throwaway copy inside the
# container and only the staged dist/ is written back to the mounted $OUT.
docker run --rm -v "$REPO":/src:ro -v "$OUT":/out "$IMAGE" bash -euo pipefail -c '
    SR=/usr/x86_64-w64-mingw32/sys-root/mingw
    cp -r /src /work && cd /work && rm -rf build build-* dist

    mingw64-cmake -G Ninja -B build-win -DBUILD_TESTING=OFF >/dev/null
    cmake --build build-win --target aetherbus

    STAGE=/work/stage
    mkdir -p "$STAGE/platforms" "$STAGE/styles"
    cp build-win/aetherbus.exe "$STAGE/"

    # Recursively copy every DLL import that resolves inside the MinGW sysroot
    # (system DLLs like kernel32 are not in the sysroot, so they are skipped).
    copydeps() {
        local f="$1"
        x86_64-w64-mingw32-objdump -p "$f" | awk "/DLL Name:/ {print \$3}" | while read -r dll; do
            if [ -f "$SR/bin/$dll" ] && [ ! -f "$STAGE/$dll" ]; then
                cp "$SR/bin/$dll" "$STAGE/"
                copydeps "$STAGE/$dll"
            fi
        done
    }
    copydeps "$STAGE/aetherbus.exe"

    # Runtime-loaded plugins are not link-time deps, so copy + walk them too.
    cp "$SR/lib/qt6/plugins/platforms/qwindows.dll" "$STAGE/platforms/"
    copydeps "$STAGE/platforms/qwindows.dll"
    if [ -f "$SR/lib/qt6/plugins/styles/qmodernwindowsstyle.dll" ]; then
        cp "$SR/lib/qt6/plugins/styles/qmodernwindowsstyle.dll" "$STAGE/styles/"
        copydeps "$STAGE/styles/qmodernwindowsstyle.dll"
    fi

    cp -r "$STAGE"/. /out/
'

echo ""
echo ">> Done. Output in: $OUT"
ls -la "$OUT"
echo ""
echo "   Copy the whole dist/windows/ folder to a Windows machine and run aetherbus.exe."
