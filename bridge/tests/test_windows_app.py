from __future__ import annotations

import unittest

from cardbridge.windows_app import (
    RESTORE_CHOICES,
    TRANSPORT_CHOICES,
    TRIGGER_CHOICES,
    choose_ui_font,
    display_choice,
    stored_choice,
)


class WindowsAppChoiceTests(unittest.TestCase):
    def test_simplified_chinese_font_is_preferred_case_insensitively(self) -> None:
        families = ("Segoe UI", "MICROSOFT YAHEI UI", "Arial")
        self.assertEqual(choose_ui_font(families), "MICROSOFT YAHEI UI")

    def test_internal_settings_are_shown_as_clear_chinese_labels(self) -> None:
        self.assertEqual(display_choice("wifi", TRANSPORT_CHOICES), "仅监听 Wi-Fi")
        self.assertEqual(
            display_choice("previous", RESTORE_CHOICES),
            "恢复说话前的麦克风",
        )
        self.assertEqual(display_choice("hold", TRIGGER_CHOICES), "按住快捷键")

    def test_display_labels_round_trip_to_config_values(self) -> None:
        for choices in (TRANSPORT_CHOICES, RESTORE_CHOICES, TRIGGER_CHOICES):
            for key, label in choices.items():
                self.assertEqual(stored_choice(label, choices), key)


if __name__ == "__main__":
    unittest.main()
