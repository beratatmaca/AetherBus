#!/usr/bin/env python3
"""Fail if any staged ELF requires a GLIBC symbol version above the ceiling.

The Linux wheel is hand-tagged (e.g. manylinux_2_35_x86_64) rather than
auditwheel-repaired, so this gate is what keeps the tag honest: it scans every
ELF in the bundle for undefined GLIBC_x.y symbol references and errors when
one exceeds the glibc version promised by the tag.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

GLIBC_REF = re.compile(r"GLIBC_(\d+)\.(\d+)")


def elf_files(root: Path):
    for path in root.rglob("*"):
        if path.is_file() and not path.is_symlink():
            with open(path, "rb") as handle:
                if handle.read(4) == b"\x7fELF":
                    yield path


def max_glibc_ref(elf: Path):
    output = subprocess.check_output(["objdump", "-T", str(elf)], text=True)
    versions = [
        (int(match.group(1)), int(match.group(2)))
        for line in output.splitlines()
        if "*UND*" in line
        for match in GLIBC_REF.finditer(line)
    ]
    return max(versions) if versions else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--max", required=True, help="glibc ceiling, e.g. 2.35")
    parser.add_argument("bundle", type=Path, help="staged _bundle directory")
    args = parser.parse_args()

    major, minor = (int(part) for part in args.max.split("."))
    ceiling = (major, minor)

    offenders = []
    checked = 0
    for elf in elf_files(args.bundle):
        checked += 1
        highest = max_glibc_ref(elf)
        if highest and highest > ceiling:
            offenders.append((elf, highest))

    if not checked:
        print(f"check_glibc.py: no ELF files found under {args.bundle}", file=sys.stderr)
        return 1
    if offenders:
        for elf, (ref_major, ref_minor) in offenders:
            print(
                f"check_glibc.py: {elf} requires GLIBC_{ref_major}.{ref_minor} "
                f"> ceiling {args.max}",
                file=sys.stderr,
            )
        return 1
    print(f"check_glibc.py: {checked} ELF files OK (ceiling GLIBC_{args.max})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
