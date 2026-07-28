from __future__ import annotations

import tempfile
import time
import unittest
import sys
from pathlib import Path

from cardbridge.config import BridgeConfig
from cardbridge.keyboard import KeyInjector
from cardbridge.voice import CaptureDevice, VoiceInputController, parse_hotkey


class FakeCaptureManager:
    def __init__(self) -> None:
        self.normal = CaptureDevice("normal-id", "Laptop Microphone")
        self.virtual = CaptureDevice("virtual-id", "CABLE Output")
        self.current = self.normal
        self.history: list[str] = []

    def list_devices(self) -> list[CaptureDevice]:
        return [self.normal, self.virtual]

    def default_device(self) -> CaptureDevice:
        return self.current

    def find(self, name_or_id: str) -> CaptureDevice | None:
        target = name_or_id.casefold()
        return next(
            (
                device
                for device in self.list_devices()
                if target in {device.id.casefold(), device.name.casefold()}
            ),
            None,
        )

    def set_default(self, device: CaptureDevice) -> None:
        self.current = device
        self.history.append(device.name)


class VoiceInputTests(unittest.TestCase):
    def test_default_enter_delay_is_1400_ms(self) -> None:
        controller, _config, _keyboard, _capture = self.make_controller()
        self.assertEqual(controller.enter_delay_s, 1.4)

    def test_windows_com_is_configured_for_ble_and_audio_mta(self) -> None:
        if sys.platform == "win32":
            self.assertEqual(getattr(sys, "coinit_flags", None), 0)

    def make_controller(
        self, *, enter_delay_s: float = 1.4
    ) -> tuple[VoiceInputController, BridgeConfig, KeyInjector, FakeCaptureManager]:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        config = BridgeConfig(Path(temporary.name) / "config.json")
        keyboard = KeyInjector(dry_run=True)
        capture = FakeCaptureManager()
        controller = VoiceInputController(
            config,
            keyboard,
            capture=capture,  # type: ignore[arg-type]
            enter_delay_s=enter_delay_s,
        )
        return controller, config, keyboard, capture

    def test_hotkey_parser_accepts_codex_and_function_key_shortcuts(self) -> None:
        self.assertEqual(parse_hotkey("ctrl+shift+d"), ("d", ["ctrl", "shift"]))
        self.assertEqual(parse_hotkey("F13"), ("f13", []))
        self.assertEqual(parse_hotkey("ctrl+cmd"), ("cmd", ["ctrl"]))
        with self.assertRaises(ValueError):
            parse_hotkey("ctrl+ctrl+d")
        with self.assertRaises(ValueError):
            parse_hotkey("ctrl")

    def test_modifier_only_hold_shortcut_has_balanced_edges(self) -> None:
        controller, config, keyboard, _capture = self.make_controller()
        config.update_voice_settings({"hotkey": "ctrl+cmd", "trigger_mode": "hold"})
        controller.start("cardputer-1")
        controller.stop("cardputer-1")
        self.assertEqual(
            keyboard.events,
            [
                {"k": "cmd", "a": "down", "m": ["ctrl"]},
                {"k": "cmd", "a": "up", "m": ["ctrl"]},
            ],
        )

    def test_hold_mode_switches_to_virtual_then_restores_before_key_up(self) -> None:
        controller, _config, keyboard, capture = self.make_controller()
        self.assertTrue(controller.start("cardputer-1"))
        self.assertEqual(capture.current, capture.virtual)
        self.assertEqual(keyboard.events[-1], {"k": "f13", "a": "down", "m": []})

        self.assertTrue(controller.stop("cardputer-1"))
        self.assertEqual(capture.current, capture.normal)
        self.assertEqual(capture.history, ["CABLE Output", "Laptop Microphone"])
        self.assertEqual(keyboard.events[-1], {"k": "f13", "a": "up", "m": []})

    def test_toggle_mode_taps_shortcut_on_both_edges(self) -> None:
        controller, config, keyboard, _capture = self.make_controller()
        config.update_voice_settings(
            {"hotkey": "ctrl+shift+d", "trigger_mode": "toggle"}
        )
        controller.start("cardputer-1", locked=True)
        controller.stop("cardputer-1")
        self.assertEqual(
            keyboard.events,
            [
                {"k": "d", "a": "down", "m": ["ctrl", "shift"]},
                {"k": "d", "a": "up", "m": ["ctrl", "shift"]},
                {"k": "d", "a": "down", "m": ["ctrl", "shift"]},
                {"k": "d", "a": "up", "m": ["ctrl", "shift"]},
            ],
        )

    def test_stop_from_another_device_cannot_end_active_voice(self) -> None:
        controller, _config, _keyboard, _capture = self.make_controller()
        controller.start("cardputer-1")
        self.assertFalse(controller.stop("cardputer-2"))
        self.assertTrue(controller.active)
        self.assertTrue(controller.stop(force=True))

    def test_optional_enter_is_sent_after_hotkey_release(self) -> None:
        controller, _config, keyboard, _capture = self.make_controller(
            enter_delay_s=0.02
        )
        controller.start("cardputer-1")
        controller.stop("cardputer-1", send_enter=True)
        self.assertNotIn("enter", [event["k"] for event in keyboard.events])
        time.sleep(0.05)
        self.assertEqual(
            keyboard.events[-3:],
            [
                {"k": "f13", "a": "up", "m": []},
                {"k": "enter", "a": "down", "m": []},
                {"k": "enter", "a": "up", "m": []},
            ],
        )

    def test_new_voice_start_cancels_pending_enter(self) -> None:
        controller, _config, keyboard, _capture = self.make_controller(
            enter_delay_s=0.03
        )
        controller.start("cardputer-1")
        controller.stop("cardputer-1", send_enter=True)
        controller.start("cardputer-1")
        time.sleep(0.06)
        self.assertNotIn("enter", [event["k"] for event in keyboard.events])
        controller.stop("cardputer-1", force=True)

    def test_forced_disconnect_does_not_schedule_enter(self) -> None:
        controller, _config, keyboard, _capture = self.make_controller(
            enter_delay_s=0.01
        )
        controller.start("cardputer-1")
        controller.stop("cardputer-1", force=True, send_enter=True)
        time.sleep(0.03)
        self.assertNotIn("enter", [event["k"] for event in keyboard.events])


if __name__ == "__main__":
    unittest.main()
