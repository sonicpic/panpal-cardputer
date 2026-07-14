from __future__ import annotations

import unittest

from cardbridge.keyboard import KEY_CODES, KeyInjector


class KeyboardTests(unittest.TestCase):
    def test_configurable_typeless_function_keys_are_supported(self) -> None:
        injector = KeyInjector(dry_run=True)
        for number in range(13, 17):
            key = f"f{number}"
            self.assertIn(key, KEY_CODES)
            self.assertTrue(injector.inject(key, "down", []))
            self.assertTrue(injector.inject(key, "up", []))


if __name__ == "__main__":
    unittest.main()
