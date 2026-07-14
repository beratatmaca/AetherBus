"""Hatchling hooks for the AetherBus launcher wheel.

Two responsibilities, both driven by CI environment variables:

* ``resolve_version()`` — version source (``[tool.hatch.version]``).
  ``AETHER_WHEEL_VERSION`` wins (set by the release workflow from its
  ``version`` job so wheels match the DEB/MSI/DMG built in the same run);
  local builds fall back to the repository ``VERSION`` file.

* ``BundleHook`` — wheel build hook. The wheel contains no Python
  extension, just a launcher plus the compiled ``aetherbus`` binary and
  its bundled Qt runtime, so it is tagged ``py3-none-<platform>`` with
  the platform coming from ``AETHER_WHEEL_PLAT`` (e.g.
  ``manylinux_2_35_x86_64``, ``win_amd64``, ``macosx_12_0_arm64``).
"""

import os
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


def resolve_version() -> str:
    env_version = os.environ.get("AETHER_WHEEL_VERSION")
    if env_version:
        return env_version
    return (Path(__file__).resolve().parent.parent / "VERSION").read_text().strip()


class BundleHook(BuildHookInterface):
    def initialize(self, version, build_data):
        platform_tag = os.environ.get("AETHER_WHEEL_PLAT")
        if not platform_tag:
            raise RuntimeError(
                "AETHER_WHEEL_PLAT is not set (e.g. manylinux_2_35_x86_64, "
                "win_amd64, macosx_12_0_arm64)"
            )
        bundle = Path(self.root) / "src" / "aetherbus" / "_bundle"
        if not any(bundle.rglob("*")):
            raise RuntimeError(
                f"{bundle} is empty — run scripts/wheel/stage.py against a "
                "Release build first"
            )
        build_data["tag"] = f"py3-none-{platform_tag}"
        build_data["pure_python"] = False
