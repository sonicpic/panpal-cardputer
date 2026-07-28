"""PanPal bridge service."""

import sys


# comtypes initializes COM as soon as it is imported. Windows BLE/WinRT and
# the audio PolicyConfig calls share the bridge asyncio thread, so select MTA
# before pycaw/comtypes can choose their default STA and cause 0x80010106.
if sys.platform == "win32":
    sys.coinit_flags = 0  # COINIT_MULTITHREADED

from ._generated_version import AGENT_VERSION as __version__
