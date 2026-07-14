from __future__ import annotations

import asyncio
import json
import logging
import os
import platform
import secrets
import socket
import subprocess
from collections.abc import Callable
from pathlib import Path
from typing import Any

from .audio import BlackHoleAudioOutput, NullAudioOutput
from .config import BridgeConfig
from .keyboard import KeyInjector
from .protocol import (
    MAX_JSON_LINE,
    ProtocolError,
    decode_message,
    encode_message,
    unpack_audio,
)

LOG = logging.getLogger("cardbridge")


class AudioDatagramProtocol(asyncio.DatagramProtocol):
    def __init__(self, app: "BridgeApp") -> None:
        self.app = app

    def datagram_received(self, data: bytes, address: tuple[str, int]) -> None:
        self.app.receive_audio(data, address)

    def error_received(self, exc: Exception) -> None:
        LOG.warning("UDP receiver error: %s", exc)


class BridgeApp:
    def __init__(
        self,
        *,
        host: str = "0.0.0.0",
        tcp_port: int = 7788,
        udp_port: int = 7789,
        config_path: Path | None = None,
        audio_device: str = "BlackHole 2ch",
        jitter_ms: int = 100,
        no_audio: bool = False,
        dry_run: bool = False,
        advertise: bool = True,
        record_path: Path | None = None,
        pair_code_factory: Callable[[], str] | None = None,
    ) -> None:
        self.host = host
        self.tcp_port = tcp_port
        self.udp_port = udp_port
        self.config = BridgeConfig(config_path)
        self.keyboard = KeyInjector(dry_run=dry_run)
        # Diagnostic tap: raw device PCM (pre-jitter) straight to a WAV file so
        # the mic->UDP->bridge path can be verified without BlackHole/Typeless.
        self._record_path = record_path
        self._record_bytes = bytearray()
        self.audio = (
            NullAudioOutput(jitter_ms)
            if no_audio
            else BlackHoleAudioOutput(audio_device, jitter_ms)
        )
        self.dry_run = dry_run
        self.advertise = advertise
        self.pair_code_factory = pair_code_factory or (lambda: f"{secrets.randbelow(1_000_000):06d}")
        self.active_tokens: dict[str, tuple[str, str]] = {}
        self.tcp_server: asyncio.AbstractServer | None = None
        self.udp_transport: asyncio.DatagramTransport | None = None
        self.zeroconf: Any = None
        self.service_info: Any = None

    async def start(self) -> None:
        self.audio.start()
        self.keyboard.check_accessibility(prompt=not self.dry_run)
        loop = asyncio.get_running_loop()
        transport, _ = await loop.create_datagram_endpoint(
            lambda: AudioDatagramProtocol(self), local_addr=(self.host, self.udp_port)
        )
        self.udp_transport = transport
        udp_socket = transport.get_extra_info("socket")
        self.udp_port = int(udp_socket.getsockname()[1])

        self.tcp_server = await asyncio.start_server(
            self.handle_client,
            self.host,
            self.tcp_port,
            limit=MAX_JSON_LINE + 1,
        )
        self.tcp_port = int(self.tcp_server.sockets[0].getsockname()[1])
        if self.advertise:
            await self._start_mdns()
        LOG.info(
            "CardBridge ready: TCP %d, UDP %d, Mac name %s",
            self.tcp_port,
            self.udp_port,
            self.config.mac_name,
        )

    def _write_wav(self) -> None:
        if self._record_path is None or not self._record_bytes:
            return
        import wave

        with wave.open(str(self._record_path), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(16000)
            wav.writeframes(bytes(self._record_bytes))
        LOG.info(
            "wrote %d samples (%.1fs) to %s",
            len(self._record_bytes) // 2,
            len(self._record_bytes) / 2 / 16000,
            self._record_path,
        )

    async def stop(self) -> None:
        self._write_wav()
        if self.service_info is not None and self.zeroconf is not None:
            try:
                await self.zeroconf.async_unregister_service(self.service_info)
            finally:
                await self.zeroconf.async_close()
            self.service_info = None
            self.zeroconf = None
        if self.tcp_server is not None:
            self.tcp_server.close()
            await self.tcp_server.wait_closed()
            self.tcp_server = None
        if self.udp_transport is not None:
            self.udp_transport.close()
            self.udp_transport = None
        self.audio.stop()

    async def serve_forever(self) -> None:
        if self.tcp_server is None:
            raise RuntimeError("bridge has not been started")
        async with self.tcp_server:
            await self.tcp_server.serve_forever()

    async def _start_mdns(self) -> None:
        try:
            from zeroconf import ServiceInfo
            from zeroconf.asyncio import AsyncZeroconf
        except ImportError as exc:
            raise RuntimeError("zeroconf is missing; install bridge/requirements.txt") from exc
        address = _local_ipv4()
        service_type = "_cardbridge._tcp.local."
        safe_name = "".join(
            character if character.isalnum() or character in "-_" else "-"
            for character in self.config.mac_name
        )
        service_name = f"{safe_name}-{self.config.bridge_id[:6]}.{service_type}"
        self.service_info = ServiceInfo(
            service_type,
            service_name,
            addresses=[socket.inet_aton(address)],
            port=self.tcp_port,
            properties={
                b"id": self.config.bridge_id.encode(),
                b"name": self.config.mac_name.encode(),
                b"udp": str(self.udp_port).encode(),
                b"version": b"1",
            },
            server=f"cardbridge-{self.config.bridge_id[:8]}.local.",
        )
        self.zeroconf = AsyncZeroconf()
        await self.zeroconf.async_register_service(self.service_info)
        LOG.info("mDNS: %s at %s", service_name, address)

    async def handle_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        peer = writer.get_extra_info("peername")
        peer_ip = str(peer[0]) if peer else "unknown"
        device_id: str | None = None
        authenticated_token: str | None = None
        pending_code: str | None = None
        pair_attempts = 0
        pressed: dict[str, list[str]] = {}
        LOG.info("control connection from %s", peer_ip)
        try:
            while authenticated_token is None:
                message = await self._read_message(reader, timeout=15)
                message_type = message["t"]
                if message_type == "hello":
                    candidate = message.get("dev_id")
                    if not isinstance(candidate, str) or not (4 <= len(candidate) <= 64):
                        raise ProtocolError("invalid dev_id")
                    device_id = candidate
                    supplied_token = message.get("token")
                    if self.config.validate(device_id, supplied_token):
                        authenticated_token = str(supplied_token)
                        await self._send(
                            writer,
                            {
                                "t": "hello_ok",
                                "mac_id": self.config.bridge_id,
                                "mac_name": self.config.mac_name,
                                "udp_port": self.udp_port,
                            },
                        )
                    elif supplied_token is not None:
                        await self._send(writer, {"t": "auth_error"})
                    else:
                        pending_code = self.pair_code_factory()
                        self._show_pair_code(pending_code, device_id)
                        await self._send(
                            writer,
                            {
                                "t": "pair_required",
                                "mac_id": self.config.bridge_id,
                                "mac_name": self.config.mac_name,
                            },
                        )
                elif message_type == "pair" and device_id and pending_code:
                    if str(message.get("code", "")) != pending_code:
                        # A six-digit code must not be brute-forceable: after a
                        # few misses drop the link. Reconnecting shows a fresh
                        # code, so an attacker cannot enumerate one code.
                        pair_attempts += 1
                        if pair_attempts >= 3:
                            raise ProtocolError("too many wrong pairing codes")
                        await self._send(writer, {"t": "pair_error"})
                        continue
                    authenticated_token = self.config.pair(device_id)
                    await self._send(
                        writer,
                        {
                            "t": "paired",
                            "mac_id": self.config.bridge_id,
                            "mac_name": self.config.mac_name,
                            "token": authenticated_token,
                            "udp_port": self.udp_port,
                        },
                    )
                elif message_type == "ping":
                    # Pair-code entry can take longer than one heartbeat window.
                    await self._send(writer, {"t": "pong"})
                else:
                    # Unknown and phase-two messages never tear down the link.
                    continue

            assert device_id is not None
            self.active_tokens[authenticated_token] = (device_id, peer_ip)
            LOG.info("device %s authenticated", device_id)
            await self._authenticated_loop(
                reader, writer, pressed, authenticated_token
            )
        except (asyncio.TimeoutError, asyncio.IncompleteReadError):
            LOG.info("control connection timed out/closed: %s", peer_ip)
        except (ProtocolError, ConnectionError, OSError) as exc:
            LOG.warning("control connection rejected from %s: %s", peer_ip, exc)
        finally:
            for key, modifiers in pressed.items():
                self.keyboard.inject(key, "up", modifiers)
            if authenticated_token and self.active_tokens.get(authenticated_token) == (
                device_id,
                peer_ip,
            ):
                self.active_tokens.pop(authenticated_token, None)
            writer.close()
            try:
                await writer.wait_closed()
            except (ConnectionError, OSError):
                pass

    async def _authenticated_loop(
        self,
        reader: asyncio.StreamReader,
        writer: asyncio.StreamWriter,
        pressed: dict[str, list[str]],
        token: str,
    ) -> None:
        missed = 0
        while True:
            try:
                message = await self._read_message(reader, timeout=5)
                missed = 0
            except asyncio.TimeoutError:
                missed += 1
                if missed >= 3:
                    raise
                await self._send(writer, {"t": "ping", "token": token})
                continue

            message_type = message["t"]
            supplied_token = message.get("token")
            if not isinstance(supplied_token, str) or not secrets.compare_digest(
                supplied_token, token
            ):
                LOG.warning("ignored unauthenticated TCP message type %s", message_type)
                continue
            if message_type == "ping":
                await self._send(writer, {"t": "pong", "token": token})
            elif message_type == "pong":
                continue
            elif message_type == "key":
                key = message.get("k")
                action = message.get("a")
                modifiers = message.get("m", [])
                if not isinstance(key, str) or not isinstance(modifiers, list):
                    continue
                allowed = {"cmd", "shift", "alt", "ctrl"}
                clean_modifiers = [item for item in modifiers if item in allowed]
                if self.keyboard.inject(key, str(action), clean_modifiers):
                    if action == "down":
                        pressed[key] = clean_modifiers
                    elif action == "up":
                        pressed.pop(key, None)
            elif message_type == "agent_list_req":
                # Protocol reservation for phase two. It is intentionally a no-op.
                continue
            else:
                # Forward-compatible parser: ignore every unknown message type.
                continue

    async def _read_message(
        self, reader: asyncio.StreamReader, timeout: float
    ) -> dict[str, Any]:
        try:
            line = await asyncio.wait_for(reader.readline(), timeout)
        except ValueError as exc:
            raise ProtocolError("JSON line exceeded limit") from exc
        if not line:
            raise asyncio.IncompleteReadError(b"", None)
        return decode_message(line)

    async def _send(self, writer: asyncio.StreamWriter, message: dict[str, Any]) -> None:
        writer.write(encode_message(message))
        await writer.drain()

    def _show_pair_code(self, code: str, device_id: str) -> None:
        # flush + log: stdout is block-buffered when redirected to a file, and
        # the code must be visible wherever the operator is watching.
        print("\n" + "=" * 50, flush=True)
        print(f"CardBridge pairing code for {device_id}: {code}", flush=True)
        print("Enter this code on the Cardputer ADV.", flush=True)
        print("=" * 50 + "\n", flush=True)
        LOG.info("pairing code for %s: %s", device_id, code)
        if platform.system() == "Darwin" and not self.dry_run:
            script = f'display notification "Code: {code}" with title "CardBridge pairing"'
            try:
                subprocess.run(
                    ["osascript", "-e", script],
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=3,
                )
            except (OSError, subprocess.TimeoutExpired):
                pass

    def receive_audio(self, datagram: bytes, address: tuple[str, int]) -> None:
        peer_ip = str(address[0])
        for token, (_device_id, active_ip) in tuple(self.active_tokens.items()):
            if active_ip != peer_ip:
                continue
            try:
                packet = unpack_audio(token, datagram)
            except ProtocolError:
                continue
            self.audio.feed(packet.sequence, packet.payload)
            if self._record_path is not None:
                self._record_bytes.extend(packet.payload)
            return
        LOG.debug("ignored unauthenticated UDP audio from %s", peer_ip)


def _local_ipv4() -> str:
    # Ask the primary LAN interfaces first. The default-route probe below can
    # pick a VPN utun address (e.g. 198.18.0.0/15 fake-IP) that LAN devices
    # cannot reach, which would advertise an unusable mDNS address.
    for interface in ("en0", "en1"):
        try:
            result = subprocess.run(
                ["ipconfig", "getifaddr", interface],
                capture_output=True,
                text=True,
                timeout=2,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            continue
        address = result.stdout.strip()
        if address:
            return address
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("8.8.8.8", 80))
        return str(probe.getsockname()[0])
    except OSError:
        return "127.0.0.1"
    finally:
        probe.close()
