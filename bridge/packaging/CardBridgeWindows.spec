# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller recipe for the Windows per-user PanPal Agent."""

from pathlib import Path

from PyInstaller.utils.hooks import collect_all, collect_submodules


bridge_dir = Path(SPECPATH).parent
sounddevice_data, sounddevice_binaries, sounddevice_hidden = collect_all("sounddevice")
bleak_data, bleak_binaries, bleak_hidden = collect_all("bleak")
pycaw_data, pycaw_binaries, pycaw_hidden = collect_all("pycaw")
tray_data, tray_binaries, tray_hidden = collect_all("infi.systray")

analysis = Analysis(
    [str(bridge_dir / "packaging" / "agent_entry.py")],
    pathex=[str(bridge_dir)],
    binaries=sounddevice_binaries + bleak_binaries + pycaw_binaries + tray_binaries,
    datas=(
        sounddevice_data
        + bleak_data
        + pycaw_data
        + tray_data
        + [(str(bridge_dir.parent / "windows" / "assets"), "windows/assets")]
    ),
    hiddenimports=(
        sounddevice_hidden
        + bleak_hidden
        + pycaw_hidden
        + tray_hidden
        + collect_submodules("zeroconf")
        + collect_submodules("winrt")
        + ["tkinter", "tkinter.ttk", "tkinter.messagebox"]
    ),
    excludes=["comtypes.test"],
    noarchive=False,
)

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
