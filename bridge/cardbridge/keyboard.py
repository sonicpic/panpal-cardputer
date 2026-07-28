from __future__ import annotations

import ctypes
import logging
from typing import Any

LOG = logging.getLogger("cardbridge.keyboard")

# Windows virtual-key codes used by the Cardputer key protocol. The protocol
# keeps the historical `cmd` name and maps it to the left Windows key.
KEY_CODES = {
    **{letter: ord(letter.upper()) for letter in "abcdefghijklmnopqrstuvwxyz"},
    **{digit: ord(digit) for digit in "0123456789"},
    "=": 0xBB,
    "-": 0xBD,
    "]": 0xDD,
    "[": 0xDB,
    "'": 0xDE,
    ";": 0xBA,
    "\\": 0xDC,
    ",": 0xBC,
    "/": 0xBF,
    ".": 0xBE,
    "`": 0xC0,
    "enter": 0x0D,
    "tab": 0x09,
    " ": 0x20,
    "backspace": 0x08,
    "escape": 0x1B,
    "cmd": 0x5B,
    "shift": 0x10,
    "alt": 0x12,
    "ctrl": 0x11,
    "f13": 0x7C,
    "f14": 0x7D,
    "f15": 0x7E,
    "f16": 0x7F,
    "home": 0x24,
    "delete_forward": 0x2E,
    "end": 0x23,
    "left": 0x25,
    "up": 0x26,
    "right": 0x27,
    "down": 0x28,
}


class _WinKeyboardInput(ctypes.Structure):
    _fields_ = [
        ("wVk", ctypes.c_ushort),
        ("wScan", ctypes.c_ushort),
        ("dwFlags", ctypes.c_uint32),
        ("time", ctypes.c_uint32),
        ("dwExtraInfo", ctypes.c_size_t),
    ]


class _WinMouseInput(ctypes.Structure):
    _fields_ = [
        ("dx", ctypes.c_int32),
        ("dy", ctypes.c_int32),
        ("mouseData", ctypes.c_uint32),
        ("dwFlags", ctypes.c_uint32),
        ("time", ctypes.c_uint32),
        ("dwExtraInfo", ctypes.c_size_t),
    ]


class _WinHardwareInput(ctypes.Structure):
    _fields_ = [
        ("uMsg", ctypes.c_uint32),
        ("wParamL", ctypes.c_ushort),
        ("wParamH", ctypes.c_ushort),
    ]


class _WinInputUnion(ctypes.Union):
    # INPUT is sized by MOUSEINPUT on 64-bit Windows even when only keyboard
    # events are sent. Keeping every union member gives SendInput the right size.
    _fields_ = [
        ("mi", _WinMouseInput),
        ("ki", _WinKeyboardInput),
        ("hi", _WinHardwareInput),
    ]


class _WinInput(ctypes.Structure):
    _anonymous_ = ("union",)
    _fields_ = [("type", ctypes.c_uint32), ("union", _WinInputUnion)]


class KeyInjector:
    def __init__(self, dry_run: bool = False) -> None:
        self.dry_run = dry_run
        self.events: list[dict[str, Any]] = []
        self.windows_user32: Any = None
        self.windows_send_input: Any = None
        if not dry_run:
            self.windows_user32 = ctypes.WinDLL("user32", use_last_error=True)
            self.windows_send_input = self.windows_user32.SendInput
            self.windows_send_input.argtypes = [
                ctypes.c_uint32,
                ctypes.POINTER(_WinInput),
                ctypes.c_int,
            ]
            self.windows_send_input.restype = ctypes.c_uint32

    def check_accessibility(self, prompt: bool = True) -> bool:
        if self.dry_run:
            return True
        # Windows blocks SendInput into a process running at a higher integrity
        # level. Ordinary applications need no separate accessibility grant.
        return self.windows_send_input is not None

    def inject(self, key: str, action: str, modifiers: list[str]) -> bool:
        key = key.lower() if len(key) == 1 and key.isalpha() else key
        if action not in {"down", "up"} or key not in KEY_CODES:
            LOG.warning("ignored invalid key event: key=%r action=%r", key, action)
            return False
        self.events.append({"k": key, "a": action, "m": list(modifiers)})
        if self.dry_run:
            LOG.info("key %s %s modifiers=%s", key, action, modifiers)
            return True
        return self._inject_windows(key, action, modifiers)

    def _send_windows_key(self, key: str, release: bool) -> bool:
        assert self.windows_send_input is not None
        flags = 0x0002 if release else 0  # KEYEVENTF_KEYUP
        event = _WinInput(
            type=1,
            ki=_WinKeyboardInput(KEY_CODES[key], 0, flags, 0, 0),
        )
        ctypes.set_last_error(0)
        sent = int(
            self.windows_send_input(1, ctypes.byref(event), ctypes.sizeof(event))
        )
        if sent == 1:
            return True
        LOG.error(
            "SendInput rejected key=%s release=%s input_size=%d winerror=%d",
            key,
            release,
            ctypes.sizeof(event),
            ctypes.get_last_error(),
        )
        return False

    def _inject_windows(self, key: str, action: str, modifiers: list[str]) -> bool:
        if self.windows_send_input is None:
            return False
        active_modifiers = [
            modifier
            for modifier in modifiers
            if modifier in KEY_CODES and modifier != key
        ]
        if action == "down":
            if not all(
                self._send_windows_key(modifier, False)
                for modifier in active_modifiers
            ):
                return False
            return self._send_windows_key(key, False)

        sent = self._send_windows_key(key, True)
        for modifier in reversed(active_modifiers):
            sent = self._send_windows_key(modifier, True) and sent
        return sent
