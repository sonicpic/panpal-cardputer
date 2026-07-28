from __future__ import annotations

import logging
import sys
import threading
from dataclasses import dataclass
from typing import Callable

from .config import BridgeConfig
from .keyboard import KeyInjector


LOG = logging.getLogger("cardbridge.voice")


@dataclass(frozen=True)
class CaptureDevice:
    id: str
    name: str


def parse_hotkey(value: str) -> tuple[str, list[str]]:
    """Parse a compact shortcut such as ``ctrl+shift+d`` or ``f13``."""

    aliases = {
        "control": "ctrl",
        "command": "cmd",
        "win": "cmd",
        "windows": "cmd",
        "option": "alt",
        "return": "enter",
        "esc": "escape",
        "space": " ",
    }
    parts = [aliases.get(part.strip().lower(), part.strip().lower()) for part in value.split("+")]
    parts = [part for part in parts if part]
    if not parts:
        raise ValueError("voice shortcut is empty")
    modifiers: list[str] = []
    for part in parts[:-1]:
        if part not in {"ctrl", "shift", "alt", "cmd"} or part in modifiers:
            raise ValueError(f"invalid voice shortcut modifier: {part}")
        modifiers.append(part)
    key = parts[-1]
    if key in {"ctrl", "shift", "alt", "cmd"}:
        if not modifiers:
            raise ValueError(
                "voice shortcut requires a key or at least two modifiers"
            )
        if key in modifiers:
            raise ValueError(f"duplicate voice shortcut modifier: {key}")
    return key, modifiers


class WindowsCaptureManager:
    """List and change Windows capture defaults through PolicyConfig.

    Windows does not expose this operation through the regular Settings API.
    pycaw wraps the same per-user PolicyConfig COM interface used by the Sound
    control panel. Failures are reported and voice input continues without a
    global-device switch.
    """

    def __init__(self) -> None:
        self.available = sys.platform == "win32"

    def list_devices(self) -> list[CaptureDevice]:
        if not self.available:
            return []
        import comtypes
        from pycaw.constants import DEVICE_STATE, EDataFlow
        from pycaw.utils import AudioUtilities

        # The Windows GUI keeps the asyncio/BLE thread in an MTA. Initializing
        # it again as STA (CoInitialize) raises RPC_E_CHANGED_MODE. Matching
        # the existing MTA is safe both in the GUI and standalone CLI.
        comtypes.CoInitializeEx(comtypes.COINIT_MULTITHREADED)
        try:
            devices = AudioUtilities.GetAllDevices(
                EDataFlow.eCapture.value, DEVICE_STATE.ACTIVE.value
            )
            result = [
                CaptureDevice(str(device.id), str(device.FriendlyName))
                for device in devices
                if getattr(device, "id", None) and getattr(device, "FriendlyName", None)
            ]
            return sorted(result, key=lambda item: item.name.casefold())
        finally:
            comtypes.CoUninitialize()

    def default_device(self) -> CaptureDevice | None:
        if not self.available:
            return None
        import comtypes
        from pycaw.utils import AudioUtilities

        comtypes.CoInitializeEx(comtypes.COINIT_MULTITHREADED)
        try:
            raw = AudioUtilities.GetMicrophone()
            if raw is None:
                return None
            device = AudioUtilities.CreateDevice(raw)
            return CaptureDevice(str(device.id), str(device.FriendlyName))
        finally:
            comtypes.CoUninitialize()

    def find(self, name_or_id: str) -> CaptureDevice | None:
        target = name_or_id.strip().casefold()
        if not target:
            return None
        devices = self.list_devices()
        exact = [item for item in devices if item.id.casefold() == target or item.name.casefold() == target]
        if exact:
            return exact[0]
        matches = [item for item in devices if target in item.name.casefold()]
        return matches[0] if matches else None

    def set_default(self, device: CaptureDevice) -> None:
        if not self.available:
            return
        import comtypes
        from pycaw.constants import ERole
        from pycaw.utils import AudioUtilities

        comtypes.CoInitializeEx(comtypes.COINIT_MULTITHREADED)
        try:
            AudioUtilities.SetDefaultDevice(
                device.id,
                roles=[ERole.eConsole, ERole.eMultimedia, ERole.eCommunications],
            )
        finally:
            comtypes.CoUninitialize()


class VoiceInputController:
    """Turn firmware voice edges into microphone routing and hotkey actions."""

    def __init__(
        self,
        config: BridgeConfig,
        keyboard: KeyInjector,
        *,
        capture: WindowsCaptureManager | None = None,
        on_change: Callable[[], None] | None = None,
        enter_delay_s: float = 1.0,
    ) -> None:
        self.config = config
        self.keyboard = keyboard
        self.capture = capture or WindowsCaptureManager()
        self.on_change = on_change
        self.active = False
        self.locked = False
        self.source_device_id = ""
        self.previous_device: CaptureDevice | None = None
        self.last_error = ""
        self.enter_delay_s = enter_delay_s
        self._enter_timer: threading.Timer | None = None
        self._lock = threading.RLock()

    def start(self, device_id: str, *, locked: bool = False) -> bool:
        with self._lock:
            self._cancel_pending_enter()
            if self.active:
                self.locked = self.locked or locked
                self._changed()
                return True
            settings = self.config.voice_settings()
            self.last_error = ""
            try:
                self.previous_device = self.capture.default_device()
                virtual = self.capture.find(str(settings["virtual_input_device"]))
                if virtual is None and sys.platform == "win32":
                    raise RuntimeError(
                        f"virtual microphone not found: {settings['virtual_input_device']}"
                    )
                if virtual is not None:
                    self.capture.set_default(virtual)
                self._shortcut(settings, starting=True)
                self.active = True
                self.locked = locked
                self.source_device_id = device_id
                LOG.info("voice input started for %s", device_id)
                self._changed()
                return True
            except Exception as exc:
                self.last_error = str(exc)
                LOG.warning("voice input could not start: %s", exc)
                self._restore(settings)
                self.active = False
                self.locked = False
                self.source_device_id = ""
                self._changed()
                return False

    def stop(
        self,
        device_id: str = "",
        *,
        force: bool = False,
        send_enter: bool = False,
    ) -> bool:
        with self._lock:
            if force:
                self._cancel_pending_enter()
            if not self.active:
                return True
            if not force and device_id and device_id != self.source_device_id:
                return False
            settings = self.config.voice_settings()
            self.last_error = ""
            ok = True
            # The requested ordering is deliberate: restore the user's normal
            # microphone before releasing/toggling the dictation shortcut.
            try:
                self._restore(settings)
            except Exception as exc:
                ok = False
                self.last_error = str(exc)
                LOG.warning("microphone restore failed: %s", exc)
            try:
                self._shortcut(settings, starting=False)
                if send_enter and not force:
                    self._schedule_enter()
            except Exception as exc:
                ok = False
                self.last_error = str(exc)
                LOG.warning("voice shortcut release failed: %s", exc)
            self.active = False
            self.locked = False
            self.source_device_id = ""
            self.previous_device = None
            LOG.info("voice input stopped")
            self._changed()
            return ok

    def _schedule_enter(self) -> None:
        self._cancel_pending_enter()

        def send_enter() -> None:
            with self._lock:
                if self._enter_timer is not timer:
                    return
                self._enter_timer = None
                down = self.keyboard.inject("enter", "down", [])
                up = self.keyboard.inject("enter", "up", [])
                if not (down and up):
                    self.last_error = "Windows rejected delayed Enter"
                    LOG.warning("Windows rejected delayed Enter")
                    self._changed()

        timer = threading.Timer(self.enter_delay_s, send_enter)
        timer.daemon = True
        self._enter_timer = timer
        timer.start()
        LOG.info("Enter scheduled in %.1f seconds", self.enter_delay_s)

    def _cancel_pending_enter(self) -> None:
        timer = self._enter_timer
        self._enter_timer = None
        if timer is not None:
            timer.cancel()

    def set_locked(self, device_id: str, locked: bool) -> None:
        with self._lock:
            if self.active and self.source_device_id == device_id:
                self.locked = locked
                self._changed()

    def snapshot(self) -> dict[str, object]:
        settings = self.config.voice_settings()
        return {
            "active": self.active,
            "locked": self.locked,
            "device_id": self.source_device_id or None,
            "last_error": self.last_error,
            "settings": settings,
        }

    def _shortcut(self, settings: dict[str, object], *, starting: bool) -> None:
        key, modifiers = parse_hotkey(str(settings["hotkey"]))
        mode = str(settings["trigger_mode"])
        if mode == "hold":
            action = "down" if starting else "up"
            if not self.keyboard.inject(key, action, modifiers):
                raise RuntimeError(f"Windows rejected voice shortcut {action}")
            return
        if not self.keyboard.inject(key, "down", modifiers):
            raise RuntimeError("Windows rejected voice shortcut down")
        if not self.keyboard.inject(key, "up", modifiers):
            raise RuntimeError("Windows rejected voice shortcut up")

    def _restore(self, settings: dict[str, object]) -> None:
        mode = str(settings["restore_mode"])
        target: CaptureDevice | None = None
        if mode == "previous":
            target = self.previous_device
        elif mode == "fixed":
            target = self.capture.find(str(settings["restore_input_device"]))
            if target is None and sys.platform == "win32":
                raise RuntimeError(
                    f"restore microphone not found: {settings['restore_input_device']}"
                )
        if target is not None:
            self.capture.set_default(target)

    def _changed(self) -> None:
        if self.on_change is not None:
            self.on_change()
