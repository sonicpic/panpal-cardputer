from __future__ import annotations

import unittest

from cardbridge.ble_transport import (
    BLE_AUDIO_TX_UUID,
    BLE_CONTROL_RX_UUID,
    BLE_CONTROL_TX_UUID,
    BleAudioReassembler,
    BleBridgeTransport,
    BleStreamWriter,
    missing_ble_characteristics,
)
from cardbridge.protocol import BLE_AUDIO_BODY_SIZE, BLE_AUDIO_FRAGMENT


class BleAudioReassemblerTests(unittest.TestCase):
    def test_reassembles_mtu_fragments_and_rejects_bad_magic(self) -> None:
        body = bytes(index % 251 for index in range(BLE_AUDIO_BODY_SIZE))
        chunks = [body[index : index + 40] for index in range(0, len(body), 40)]
        reassembler = BleAudioReassembler()
        self.assertIsNone(
            reassembler.feed(BLE_AUDIO_FRAGMENT.pack(b"XX", 9, 0, 1) + body)
        )
        result = None
        for index, chunk in enumerate(chunks):
            result = reassembler.feed(
                BLE_AUDIO_FRAGMENT.pack(b"BA", 9, index, len(chunks)) + chunk
            )
        self.assertEqual(result, body)


class BleStreamWriterTests(unittest.IsolatedAsyncioTestCase):
    async def test_control_stream_is_split_to_gatt_write_limit(self) -> None:
        class Characteristic:
            max_write_without_response_size = 23

        class Services:
            def get_characteristic(self, _uuid: str) -> Characteristic:
                return Characteristic()

        class Client:
            services = Services()
            is_connected = True

            def __init__(self) -> None:
                self.writes: list[bytes] = []

            async def write_gatt_char(
                self, _uuid: str, value: bytes, *, response: bool
            ) -> None:
                self.writes.append(value)
                self.assert_response = response

            async def disconnect(self) -> None:
                self.is_connected = False

        client = Client()
        writer = BleStreamWriter(client, "fake-address")
        payload = b'{"t":"agent_status","value":"' + b"x" * 100 + b'"}\n'
        writer.write(payload)
        await writer.drain()

        self.assertEqual(b"".join(client.writes), payload)
        self.assertTrue(all(len(item) <= 23 for item in client.writes))
        self.assertFalse(client.assert_response)
        writer.close()
        await writer.wait_closed()
        self.assertTrue(writer.is_closing())


class BleDiscoveryTests(unittest.IsolatedAsyncioTestCase):
    async def test_missing_characteristics_are_reported_before_subscribe(self) -> None:
        class Services:
            def get_characteristic(self, uuid: str) -> object | None:
                return object() if uuid == BLE_CONTROL_RX_UUID else None

        self.assertEqual(
            missing_ble_characteristics(Services()),
            (BLE_CONTROL_TX_UUID, BLE_AUDIO_TX_UUID),
        )

    async def test_reconnect_failures_use_bounded_backoff(self) -> None:
        transport = BleBridgeTransport(object())
        transport._schedule_retry("device")
        first = transport.retry_after["device"]
        transport._schedule_retry("device")
        second = transport.retry_after["device"]

        self.assertEqual(transport.failures["device"], 2)
        self.assertGreater(second, first)
        for _ in range(10):
            transport._schedule_retry("device")
        self.assertEqual(transport.failures["device"], 5)

    async def test_scanner_is_paused_and_resumed_around_connection(self) -> None:
        class Scanner:
            def __init__(self) -> None:
                self.history: list[str] = []

            async def start(self) -> None:
                self.history.append("start")

            async def stop(self) -> None:
                self.history.append("stop")

        transport = BleBridgeTransport(object())
        scanner = Scanner()
        transport.scanner = scanner
        transport._scanner_running = True

        await transport._pause_scanner()
        self.assertFalse(transport._scanner_running)
        await transport._resume_scanner()

        self.assertTrue(transport._scanner_running)
        self.assertEqual(scanner.history, ["stop", "start"])

    async def test_scanner_recovers_after_windows_radio_startup_delay(self) -> None:
        class Scanner:
            def __init__(self) -> None:
                self.start_calls = 0

            async def start(self) -> None:
                self.start_calls += 1
                if self.start_calls == 1:
                    raise OSError("Bluetooth service is starting")

        transport = BleBridgeTransport(object())
        scanner = Scanner()
        transport.scanner = scanner

        await transport.ensure_scanning()
        self.assertFalse(transport._scanner_running)
        self.assertEqual(scanner.start_calls, 1)

        transport._next_scanner_retry = 0.0
        await transport.ensure_scanning()
        self.assertTrue(transport._scanner_running)
        self.assertEqual(scanner.start_calls, 2)

    async def test_stale_windows_gatt_bond_is_repaired_only_once(self) -> None:
        class App:
            def __init__(self) -> None:
                self.status_changes = 0

            def _status_changed(self) -> None:
                self.status_changes += 1

        class Client:
            def __init__(self) -> None:
                self.unpair_calls = 0

            async def unpair(self) -> None:
                self.unpair_calls += 1

        app = App()
        client = Client()
        transport = BleBridgeTransport(app)
        missing = (BLE_CONTROL_RX_UUID,)

        self.assertTrue(
            await transport._repair_stale_gatt_cache(client, "device", missing)
        )
        self.assertFalse(
            await transport._repair_stale_gatt_cache(client, "device", missing)
        )
        self.assertEqual(client.unpair_calls, 1)
        self.assertEqual(app.status_changes, 1)
        self.assertIn("device", transport.retry_after)

    async def test_failed_gatt_refresh_reports_actionable_error(self) -> None:
        class App:
            def _status_changed(self) -> None:
                pass

        class Client:
            async def unpair(self) -> None:
                raise OSError("radio unavailable")

        transport = BleBridgeTransport(App())
        self.assertFalse(
            await transport._repair_stale_gatt_cache(
                Client(), "device", (BLE_CONTROL_RX_UUID,)
            )
        )
        self.assertIn("自动刷新", transport.last_error)


if __name__ == "__main__":
    unittest.main()
