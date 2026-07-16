from __future__ import annotations

import asyncio
import json
import socket
import struct
import tempfile
import unittest
from unittest.mock import AsyncMock, patch
from pathlib import Path

from cardbridge.protocol import encode_message, pack_audio
from cardbridge.server import BridgeApp
from fake_device import FakeDevice


class ServerEndToEndTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.app = BridgeApp(
            host="127.0.0.1",
            tcp_port=0,
            udp_port=0,
            config_path=Path(self.temporary.name) / "config.json",
            no_audio=True,
            dry_run=True,
            advertise=False,
            enable_agents=False,
            pair_code_factory=lambda: "483291",
        )
        await self.app.start()

    async def asyncTearDown(self) -> None:
        await self.app.stop()
        self.temporary.cleanup()

    async def read(self, reader: asyncio.StreamReader) -> dict[str, object]:
        return json.loads(await asyncio.wait_for(reader.readline(), 2))

    async def test_pair_key_audio_and_forward_compatibility(self) -> None:
        reader, writer = await asyncio.open_connection("127.0.0.1", self.app.tcp_port)
        writer.write(encode_message({"t": "hello", "dev_id": "aabbccddeeff", "token": None}))
        await writer.drain()
        pair_required = await self.read(reader)
        self.assertEqual(pair_required["t"], "pair_required")
        self.assertEqual(pair_required["compatibility"], "legacy")
        self.assertEqual(pair_required["protocol"], {"major": 1, "minor": 0})

        # Pre-auth heartbeat keeps a slow human pairing flow alive.
        writer.write(encode_message({"t": "ping"}))
        await writer.drain()
        self.assertEqual((await self.read(reader))["t"], "pong")

        writer.write(encode_message({"t": "pair", "code": "483291"}))
        await writer.drain()
        paired = await self.read(reader)
        self.assertEqual(paired["t"], "paired")
        self.assertEqual(paired["compatibility"], "legacy")
        token = str(paired["token"])
        self.assertEqual(len(bytes.fromhex(token)), 32)

        # A syntactically valid post-handshake message without the token is ignored.
        writer.write(encode_message({"t": "key", "k": "x", "m": [], "a": "down"}))
        for action in ("down", "up"):
            writer.write(
                encode_message(
                    {"t": "key", "k": "f13", "m": [], "a": action, "token": token}
                )
            )
        writer.write(
            encode_message({"t": "future_type", "anything": True, "token": token})
        )
        writer.write(encode_message({"t": "agent_list_req", "token": token}))
        writer.write(encode_message({"t": "ping", "token": token}))
        await writer.drain()
        received = [await self.read(reader) for _ in range(3)]
        self.assertEqual(
            {item["t"] for item in received},
            {"agent_status", "agent_list", "pong"},
        )

        payload = struct.pack("<320h", *([1234] * 320))
        valid = pack_audio(token, 7, 140, payload)
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.sendto(valid, ("127.0.0.1", self.app.udp_port))
        udp.sendto(valid[:-1] + bytes([valid[-1] ^ 1]), ("127.0.0.1", self.app.udp_port))
        udp.close()
        await asyncio.sleep(0.1)

        self.assertEqual(
            self.app.keyboard.events[:2],
            [
                {"k": "f13", "a": "down", "m": []},
                {"k": "f13", "a": "up", "m": []},
            ],
        )
        self.assertEqual(self.app.audio.jitter.received, 1)
        writer.close()
        await writer.wait_closed()

        # The persisted token authenticates directly after a bridge/device restart.
        reader2, writer2 = await asyncio.open_connection("127.0.0.1", self.app.tcp_port)
        writer2.write(
            encode_message({"t": "hello", "dev_id": "aabbccddeeff", "token": token})
        )
        await writer2.drain()
        hello_ok = await self.read(reader2)
        self.assertEqual(hello_ok["t"], "hello_ok")
        self.assertEqual(hello_ok["compatibility"], "legacy")
        writer2.close()
        await writer2.wait_closed()

    async def test_incompatible_protocol_gets_upgrade_response_before_pairing(self) -> None:
        reader, writer = await asyncio.open_connection("127.0.0.1", self.app.tcp_port)
        writer.write(
            encode_message(
                {
                    "t": "hello",
                    "dev_id": "aabbccddeeff",
                    "token": None,
                    "device": {"firmware": "9.0.0"},
                    "protocol": {"major": 99, "minor": 0},
                }
            )
        )
        await writer.drain()
        response = await self.read(reader)
        self.assertEqual(response["t"], "upgrade_required")
        self.assertEqual(response["reason"], "protocol_major")
        self.assertEqual(await asyncio.wait_for(reader.readline(), 2), b"")
        writer.close()
        await writer.wait_closed()

    async def test_pair_code_brute_force_disconnects(self) -> None:
        reader, writer = await asyncio.open_connection("127.0.0.1", self.app.tcp_port)
        writer.write(encode_message({"t": "hello", "dev_id": "aabbccddeeff", "token": None}))
        await writer.drain()
        self.assertEqual((await self.read(reader))["t"], "pair_required")
        for _ in range(2):
            writer.write(encode_message({"t": "pair", "code": "000000"}))
            await writer.drain()
            self.assertEqual((await self.read(reader))["t"], "pair_error")
        writer.write(encode_message({"t": "pair", "code": "000001"}))
        await writer.drain()
        # The third wrong code closes the connection instead of answering.
        self.assertEqual(await asyncio.wait_for(reader.readline(), 2), b"")
        writer.close()
        await writer.wait_closed()

    async def test_shipped_fake_device_runs_end_to_end(self) -> None:
        def run_fake() -> None:
            device = FakeDevice(
                "127.0.0.1", self.app.tcp_port, self.app.udp_port,
                device_id="simulator-device",
            )
            try:
                device.connect("483291")
                device.type_text("Az!\n", interval=0)
                device.key("f13", "down")
                device.key("f13", "up")
                device.send({"t": "agent_list_req"})
                device.send_sine(seconds=0.06)
            finally:
                device.close()

        await asyncio.to_thread(run_fake)
        await asyncio.sleep(0.1)
        keys = [(event["k"], event["a"]) for event in self.app.keyboard.events]
        self.assertIn(("f13", "down"), keys)
        self.assertIn(("f13", "up"), keys)
        self.assertGreaterEqual(self.app.audio.jitter.received, 3)


class MdnsLifecycleTests(unittest.IsolatedAsyncioTestCase):
    async def test_async_mdns_registers_without_blocking_event_loop(self) -> None:
        try:
            import zeroconf  # noqa: F401
        except ImportError:
            self.skipTest("zeroconf is optional for dependency-free source tests")
        with tempfile.TemporaryDirectory() as directory:
            app = BridgeApp(
                host="127.0.0.1",
                tcp_port=0,
                udp_port=0,
                config_path=Path(directory) / "config.json",
                no_audio=True,
                dry_run=True,
                advertise=True,
                enable_agents=False,
            )
            await asyncio.wait_for(app.start(), 5)
            self.assertIsNotNone(app.service_info)
            await asyncio.wait_for(app.stop(), 5)

    async def test_network_change_refreshes_mdns_without_restarting_bridge(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            app = BridgeApp(
                config_path=Path(directory) / "config.json",
                no_audio=True,
                dry_run=True,
                advertise=True,
                enable_agents=False,
            )
            app.lan_address = "192.168.1.10"
            app.network_available = True
            app.service_state = "ready"
            app._restart_mdns = AsyncMock()

            with patch("cardbridge.server._local_ipv4", return_value="192.168.1.11"):
                await app._refresh_network()

            self.assertEqual(app.lan_address, "192.168.1.11")
            self.assertTrue(app.network_available)
            app._restart_mdns.assert_awaited_once()

            with patch("cardbridge.server._local_ipv4", return_value="127.0.0.1"):
                await app._refresh_network()
            self.assertFalse(app.network_available)
            self.assertIn("network", app.status_snapshot()["agent"]["issues"])


class StartupDegradationTests(unittest.IsolatedAsyncioTestCase):
    async def test_missing_blackhole_does_not_stop_keyboard_or_network_bridge(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            app = BridgeApp(
                host="127.0.0.1",
                tcp_port=0,
                udp_port=0,
                config_path=Path(directory) / "config.json",
                dry_run=True,
                advertise=False,
                enable_agents=False,
            )
            with patch(
                "cardbridge.audio.BlackHoleAudioOutput.start",
                side_effect=RuntimeError("BlackHole 2ch is missing"),
            ):
                await app.start()
            try:
                snapshot = app.status_snapshot()
                self.assertEqual(snapshot["agent"]["state"], "ready")
                self.assertFalse(snapshot["audio"]["running"])
                self.assertIn("BlackHole", snapshot["agent"]["last_error"])
                self.assertIsNotNone(app.tcp_server)
            finally:
                await app.stop()


if __name__ == "__main__":
    unittest.main()
