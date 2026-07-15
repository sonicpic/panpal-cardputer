from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path

from cardbridge.config import BridgeConfig
from cardbridge.protocol import (
    AUDIO_PAYLOAD_SIZE,
    ProtocolError,
    decode_message,
    encode_message,
    pack_audio,
    unpack_audio,
)


class ProtocolTests(unittest.TestCase):
    def test_audio_round_trip_and_authentication(self) -> None:
        token = "12" * 32
        payload = bytes((index % 251 for index in range(AUDIO_PAYLOAD_SIZE)))
        datagram = pack_audio(token, 42, 123_456, payload)
        packet = unpack_audio(token, datagram)
        self.assertEqual(packet.sequence, 42)
        self.assertEqual(packet.timestamp_ms, 123_456)
        self.assertEqual(packet.payload, payload)

        corrupted = datagram[:-1] + bytes([datagram[-1] ^ 1])
        with self.assertRaises(ProtocolError):
            unpack_audio(token, corrupted)

    def test_json_line_round_trip(self) -> None:
        message = {"t": "key", "k": "\\", "m": ["cmd"], "a": "down"}
        self.assertEqual(decode_message(encode_message(message)), message)
        with self.assertRaises(ProtocolError):
            decode_message(json.dumps(["not", "an", "object"]).encode())

    def test_json_line_uses_compact_utf8_for_session_titles(self) -> None:
        message = {"t": "agent_status", "title": "会话管理与宠物动画"}
        encoded = encode_message(message)
        self.assertNotIn(b"\\u", encoded)
        self.assertEqual(decode_message(encoded), message)


class ConfigTests(unittest.TestCase):
    def test_pairing_persists_a_32_byte_random_token(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            config = BridgeConfig(path)
            token = config.pair("device-1")
            self.assertEqual(len(bytes.fromhex(token)), 32)
            self.assertTrue(config.validate("device-1", token))
            self.assertFalse(config.validate("device-2", token))
            reloaded = BridgeConfig(path)
            self.assertEqual(reloaded.token_for("device-1"), token)
            self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)


if __name__ == "__main__":
    unittest.main()
