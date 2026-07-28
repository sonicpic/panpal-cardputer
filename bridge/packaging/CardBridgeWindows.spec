# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller recipe for the Windows per-user PanPal Agent."""

from pathlib import Path

from PyInstaller.utils.hooks import collect_submodules


bridge_dir = Path(SPECPATH).parent
analysis = Analysis(
    [str(bridge_dir / "packaging" / "agent_entry.py")],
    pathex=[str(bridge_dir)],
    binaries=[],
    datas=[(str(bridge_dir.parent / "windows" / "assets"), "windows/assets")],
    hiddenimports=(
        collect_submodules("zeroconf")
        + collect_submodules("winrt")
        + [
            "bleak.backends.winrt.client",
            "bleak.backends.winrt.scanner",
            "infi.systray",
            "pycaw.constants",
            "pycaw.utils",
            "tkinter",
            "tkinter.ttk",
            "tkinter.messagebox",
        ]
    ),
    excludes=[
        "bleak.backends.bluezdbus",
        "bleak.backends.corebluetooth",
        "bleak.backends.p4android",
        "comtypes.test",
        "numpy",
    ],
    noarchive=False,
)

# The sounddevice wheel ships an optional ASIO PortAudio build. PanPal uses the
# regular Windows audio endpoint API, so carrying the ASIO DLL only increases
# the installed and compressed package sizes.
analysis.binaries = [
    entry for entry in analysis.binaries
    if not entry[0].lower().endswith("libportaudio64bit-asio.dll")
]

python_archive = PYZ(analysis.pure)

executable = EXE(
    python_archive,
    analysis.scripts,
    [],
    exclude_binaries=True,
    name="CardBridge",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    icon=str(bridge_dir.parent / "windows" / "assets" / "codex-deck-green.ico"),
)

collection = COLLECT(
    executable,
    analysis.binaries,
    analysis.datas,
    strip=False,
    upx=False,
    name="CardBridge",
)
