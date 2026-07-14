"""Python launcher for the AetherBus serial/CAN/Ethernet sniffer GUI.

The wheel ships the compiled Qt application plus its Qt runtime under
``aetherbus/_bundle/``; this package only locates and executes it.
"""

from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("aetherbus")
except PackageNotFoundError:  # running from a source checkout
    __version__ = "0.0.0"
