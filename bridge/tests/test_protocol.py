from __future__ import annotations

import json
import hashlib
import hmac
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from cardbridge._generated_version import CONFIG_SCHEMA
from cardbridge.config import BridgeConfig
from cardbridge.protocol import (
    BLE_ADPCM_BYTES,
    BLE_AUDIO_BODY_HEADER,
    AUDIO_PAYLOAD_SIZE,
    MAX_JSON_LINE,
    ProtocolError,
    decode_message,
    encode_message,
    pack_audio,
    token_bytes,
    unpack_ble_audio,
    unpack_audio,
)


def encode_test_adpcm(samples: list[int]) -> tuple[int, int, bytes]:
    index_table = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)
    step_table = (
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
        143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
        494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
        1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
        4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
        11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
        27086, 29794, 32767,
    )
    initial = samples[0]
    predictor = initial
    index = 0
    output = bytearray(BLE_ADPCM_BYTES)
    for sample_index, sample in enumerate(samples[1:]):
        step = step_table[index]
        difference = sample - predictor
        code = 0
        if difference < 0:
            code = 8
            difference = -difference
        delta = step >> 3
        if difference >= step:
            code |= 4
            difference -= step
            delta += step
        if difference >= step >> 1:
            code |= 2
            difference -= step >> 1
            delta += step >> 1
        if difference >= step >> 2:
            code |= 1
            delta += step >> 2
        predictor += -delta if code & 8 else delta
        predictor = max(-32768, min(32767, predictor))
        index = max(0, min(88, index + index_table[code]))
        output[sample_index // 2] |= code << (4 if sample_index & 1 else 0)
    return initial, 0, bytes(output)


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

    def test_ble_adpcm_round_trip_and_hmac(self) -> None:
        token = "34" * 32
        samples = [max(-30000, min(30000, (index - 160) * 170)) for index in range(320)]
        predictor, adpcm_index, encoded = encode_test_adpcm(samples)
        authenticated = BLE_AUDIO_BODY_HEADER.pack(
            7, 42, 840, predictor, adpcm_index, 0
        ) + encoded
        signature = hmac.new(
            token_bytes(token), authenticated, hashlib.sha256
        ).digest()[:8]
        packet = unpack_ble_audio(token, authenticated + signature)
        decoded = struct.unpack("<320h", packet.payload)
        self.assertEqual((packet.stream_id, packet.sequence, packet.timestamp_ms), (7, 42, 840))
        self.assertEqual(len(decoded), 320)
        self.assertLess(max(abs(left - right) for left, right in zip(samples, decoded)), 5000)
        with self.assertRaises(ProtocolError):
            unpack_ble_audio(token, authenticated + bytes([signature[0] ^ 1]) + signature[1:])

    def test_json_line_uses_compact_utf8_for_session_titles(self) -> None:
        message = {"t": "agent_status", "title": "会话管理与宠物动画"}
        encoded = encode_message(message)
        self.assertNotIn(b"\\u", encoded)
        self.assertEqual(decode_message(encoded), message)

    def test_json_encoder_rejects_records_above_device_limit(self) -> None:
        with self.assertRaises(ProtocolError):
            encode_message({"t": "oversized", "value": "x" * MAX_JSON_LINE})


class ConfigTests(unittest.TestCase):
    def test_production_config_directory_is_private(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            config_directory = home / ".cardbridge"
            config_directory.mkdir(mode=0o755)
            with patch.object(Path, "home", return_value=home):
                BridgeConfig()
            if sys.platform != "win32":
                self.assertEqual(os.stat(config_directory).st_mode & 0o777, 0o700)
                self.assertEqual(
                    os.stat(config_directory / "config.json").st_mode & 0o777,
                    0o600,
                )

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
            self.assertEqual(reloaded.data["config_schema"], CONFIG_SCHEMA)
            self.assertNotIn("version", reloaded.data)
            if sys.platform != "win32":
                self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)

    def test_plaintext_pairing_token_is_migrated_to_external_store(self) -> None:
        class MemoryTokenStore:
            def __init__(self) -> None:
                self.tokens: dict[str, str] = {}

            def get(self, device_id: str) -> str | None:
                return self.tokens.get(device_id)

            def put(self, device_id: str, token: str) -> None:
                self.tokens[device_id] = token

            def delete(self, device_id: str) -> None:
                self.tokens.pop(device_id, None)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            token = "ab" * 32
            path.write_text(
                json.dumps(
                    {
                        "config_schema": CONFIG_SCHEMA,
                        "bridge_id": "bridge-dpapi-test",
                        "mac_name": "Test PC",
                        "devices": {
                            "device-1": {
                                "name": "Cardputer",
                                "token": token,
                                "paired_at": 123,
                            }
                        },
                    }
                ),
                encoding="utf-8",
            )
            store = MemoryTokenStore()
            config = BridgeConfig(path, token_store=store)

            self.assertEqual(config.token_for("device-1"), token)
            self.assertEqual(store.tokens["device-1"], token)
            persisted = json.loads(path.read_text(encoding="utf-8"))
            self.assertNotIn("token", persisted["devices"]["device-1"])

            reloaded = BridgeConfig(path, token_store=store)
            self.assertTrue(reloaded.validate("device-1", token))
            self.assertTrue(reloaded.unpair("device-1"))
            self.assertNotIn("device-1", store.tokens)

    def test_legacy_config_schema_is_migrated_without_changing_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(
                json.dumps(
                    {
                        "version": 1,
                        "bridge_id": "bridge-123",
                        "mac_name": "Test PC",
                        "devices": {},
                    }
                ),
                encoding="utf-8",
            )
            config = BridgeConfig(path)
            self.assertEqual(config.bridge_id, "bridge-123")
            self.assertEqual(config.mac_name, "Test PC")
            persisted = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(persisted["config_schema"], CONFIG_SCHEMA)
            self.assertNotIn("version", persisted)

    def test_voice_defaults_are_added_without_exposing_pairing_secrets(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            config = BridgeConfig(path)
            voice = config.voice_settings()
            self.assertEqual(voice["virtual_input_device"], "CABLE Output")
            self.assertEqual(voice["restore_mode"], "previous")
            self.assertEqual(voice["trigger_mode"], "hold")
            self.assertEqual(config.bridge_transport(), "both")
            self.assertEqual(config.quota_settings()["poll_seconds"], 120)

    def test_custom_quota_key_uses_separate_secret_store(self) -> None:
        class MemoryTokenStore:
            def __init__(self) -> None:
                self.tokens: dict[str, str] = {}

            def get(self, key: str) -> str | None:
                return self.tokens.get(key)

            def put(self, key: str, token: str) -> None:
                self.tokens[key] = token

            def delete(self, key: str) -> None:
                self.tokens.pop(key, None)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            quota_store = MemoryTokenStore()
            config = BridgeConfig(path, quota_token_store=quota_store)
            settings = config.update_quota_settings(
                {
                    "source": "custom",
                    "url": "https://quota.example.test/v1",
                    "account_label": "codex-1",
                    "api_key": "read-only-secret",
                }
            )

            self.assertTrue(settings["api_key_present"])
            self.assertNotIn("api_key", settings)
            self.assertEqual(
                config.quota_settings(include_secret=True)["api_key"],
                "read-only-secret",
            )
            persisted = json.loads(path.read_text(encoding="utf-8"))
            self.assertNotIn("api_key", persisted["quota"])

            config.update_quota_settings({"clear_api_key": True})
            self.assertFalse(config.quota_settings()["api_key_present"])

    def test_newer_config_schema_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            path.write_text(
                json.dumps({"config_schema": CONFIG_SCHEMA + 1}),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "newer than supported"):
                BridgeConfig(path)


if __name__ == "__main__":
    unittest.main()
