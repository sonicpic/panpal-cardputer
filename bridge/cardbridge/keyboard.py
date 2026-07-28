from __future__ import annotations

import ctypes
import logging
import platform
from typing import Any

LOG = logging.getLogger("cardbridge.keyboard")

# macOS ANSI virtual key codes. Printable input intentionally follows the US
# layout because phase one explicitly targets English/code/shortcut workflows.
KEY_CODES = {
    "a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5,
    "z": 6, "x": 7, "c": 8, "v": 9, "b": 11, "q": 12,
    "w": 13, "e": 14, "r": 15, "y": 16, "t": 17,
    "1": 18, "2": 19, "3": 20, "4": 21, "6": 22, "5": 23,
    "=": 24, "9": 25, "7": 26, "-": 27, "8": 28, "0": 29,
    "]": 30, "o": 31, "u": 32, "[": 33, "i": 34, "p": 35,
    "enter": 36, "l": 37, "j": 38, "'": 39, "k": 40, ";": 41,
    "\\": 42, ",": 43, "/": 44, "n": 45, "m": 46, ".": 47,
    "tab": 48, " ": 49, "`": 50, "backspace": 51, "escape": 53,
    "cmd": 55, "shift": 56, "alt": 58, "ctrl": 59,
    "f13": 105, "f14": 107, "f15": 113, "f16": 106,
    "home": 115, "delete_forward": 117, "end": 119,
    "left": 123, "right": 124, "down": 125, "up": 126,
}

# Windows virtual-key codes.  The protocol uses the macOS-compatible `cmd`
# name; on Windows it deliberately maps to the left Windows key so shortcuts
# retain their familiar system-level meaning.
WINDOWS_KEY_CODES = {
    **{letter: ord(letter.upper()) for letter in "abcdefghijklmnopqrstuvwxyz"},
    **{digit: ord(digit) for digit in "0123456789"},
    "=": 0xBB, "-": 0xBD, "]": 0xDD, "[": 0xDB, "'": 0xDE,
    ";": 0xBA, "\\": 0xDC, ",": 0xBC, "/": 0xBF, ".": 0xBE,
    "`": 0xC0, "enter": 0x0D, "tab": 0x09, " ": 0x20,
    "backspace": 0x08, "escape": 0x1B, "cmd": 0x5B, "shift": 0x10,
    "alt": 0x12, "ctrl": 0x11, "f13": 0x7C, "f14": 0x7D,
    "f15": 0x7E, "f16": 0x7F, "home": 0x24, "delete_forward": 0x2E,
    "end": 0x23, "left": 0x25, "up": 0x26, "right": 0x27, "down": 0x28,
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
    # INPUT's native union is sized by MOUSEINPUT, which is larger than
    # KEYBDINPUT on 64-bit Windows. Omitting the unused variants shrinks the
    # ctypes structure from the required 40 bytes to 32, and SendInput rejects
    # every event because cbSize no longer matches sizeof(INPUT).
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
        self.quartz: Any = None
        self.accessibility: Any = None
        self.windows_user32: Any = None
        self.windows_send_input: Any = None
        self.platform = platform.system()
        if self.platform == "Windows":
            self.windows_user32 = ctypes.WinDLL("user32", use_last_error=True)
            self.windows_send_input = self.windows_user32.SendInput
            self.windows_send_input.argtypes = [
                ctypes.c_uint32,
                ctypes.POINTER(_WinInput),
                ctypes.c_int,
            ]
            self.windows_send_input.restype = ctypes.c_uint32
        if not dry_run and self.platform != "Windows":
            try:
                import Quartz
                self.quartz = Quartz
                try:
                    import ApplicationServices
                    self.accessibility = ApplicationServices
                except ImportError:
                    # Compatibility with PyObjC releases that re-exported AX
                    # trust functions from Quartz.
                    self.accessibility = Quartz
            except ImportError:
                LOG.error("PyObjC is unavailable; install the bridge requirements")

    def check_accessibility(self, prompt: bool = True) -> bool:
        if self.dry_run:
            return True
        if self.platform == "Windows":
            # SendInput is a normal user-session API. There is no macOS-style
            # Accessibility consent dialog, although Windows prevents input
            # injection into a higher-integrity (administrator) application.
            return self.windows_send_input is not None
        if self.accessibility is None:
            return False
        accessibility = self.accessibility
        try:
            options = {accessibility.kAXTrustedCheckOptionPrompt: prompt}
            trusted = bool(accessibility.AXIsProcessTrustedWithOptions(options))
        except (AttributeError, TypeError):
            try:
                trusted = bool(accessibility.AXIsProcessTrusted())
            except AttributeError:
                LOG.error("Accessibility trust API is unavailable in this PyObjC installation")
                return False
        if not trusted and prompt:
            LOG.error(
                "Accessibility permission is required: System Settings > Privacy & Security > Accessibility"
            )
        return trusted

    def inject(self, key: str, action: str, modifiers: list[str]) -> bool:
        key = key.lower() if len(key) == 1 and key.isalpha() else key
        if action not in {"down", "up"} or key not in KEY_CODES:
            LOG.warning("ignored invalid key event: key=%r action=%r", key, action)
            return False
        event_record = {"k": key, "a": action, "m": list(modifiers)}
        self.events.append(event_record)
        if self.dry_run:
            LOG.info("key %s %s modifiers=%s", key, action, modifiers)
            return True
        if self.platform == "Windows":
            return self._inject_windows(key, action, modifiers)
        if self.quartz is None:
            return False

        q = self.quartz
        event = q.CGEventCreateKeyboardEvent(None, KEY_CODES[key], action == "down")
        flags = 0
        flag_names = {
            "cmd": q.kCGEventFlagMaskCommand,
            "shift": q.kCGEventFlagMaskShift,
            "alt": q.kCGEventFlagMaskAlternate,
            "ctrl": q.kCGEventFlagMaskControl,
        }
        for modifier in modifiers:
            flags |= flag_names.get(modifier, 0)
        q.CGEventSetFlags(event, flags)
        q.CGEventPost(q.kCGHIDEventTap, event)
        return True

    def _send_windows_key(self, key: str, release: bool) -> bool:
        assert self.windows_send_input is not None
        virtual_key = WINDOWS_KEY_CODES[key]
        flags = 0x0002 if release else 0  # KEYEVENTF_KEYUP
        event = _WinInput(
            type=1,  # INPUT_KEYBOARD
            ki=_WinKeyboardInput(virtual_key, 0, flags, 0, 0),
        )
        ctypes.set_last_error(0)
        sent = int(
            self.windows_send_input(1, ctypes.byref(event), ctypes.sizeof(event))
        )
        if sent != 1:
            error = ctypes.get_last_error()
            LOG.error(
                "SendInput rejected key=%s release=%s input_size=%d winerror=%d",
                key,
                release,
                ctypes.sizeof(event),
                error,
            )
            return False
        return True

    def _inject_windows(self, key: str, action: str, modifiers: list[str]) -> bool:
        if self.windows_send_input is None or key not in WINDOWS_KEY_CODES:
            return False
        # Windows keyboard events do not carry a modifier bitmask equivalent to
        # Quartz. Wrap non-modifier keys in explicit modifier down/up events.
        active_modifiers = [
            modifier for modifier in modifiers
            if modifier in WINDOWS_KEY_CODES and modifier != key
        ]
        if action == "down":
            if not all(self._send_windows_key(modifier, False) for modifier in active_modifiers):
                return False
            return self._send_windows_key(key, False)

        sent = self._send_windows_key(key, True)
        for modifier in reversed(active_modifiers):
            sent = self._send_windows_key(modifier, True) and sent
        return sent
