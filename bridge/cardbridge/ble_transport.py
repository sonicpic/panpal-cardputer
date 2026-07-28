from __future__ import annotations

import asyncio
import logging
import sys
from collections.abc import Callable
from typing import Any

from .protocol import (
    BLE_AUDIO_BODY_SIZE,
    BLE_AUDIO_FRAGMENT,
    ProtocolError,
    unpack_ble_audio,
)


LOG = logging.getLogger("cardbridge.ble")

BLE_SERVICE_UUID = "7e400001-b5a3-f393-e0a9-e50e24dcca9e"
BLE_CONTROL_RX_UUID = "7e400002-b5a3-f393-e0a9-e50e24dcca9e"
BLE_CONTROL_TX_UUID = "7e400003-b5a3-f393-e0a9-e50e24dcca9e"
BLE_AUDIO_TX_UUID = "7e400004-b5a3-f393-e0a9-e50e24dcca9e"
BLE_REQUIRED_CHARACTERISTICS = (
    BLE_CONTROL_RX_UUID,
    BLE_CONTROL_TX_UUID,
    BLE_AUDIO_TX_UUID,
)


def missing_ble_characteristics(services: Any) -> tuple[str, ...]:
    return tuple(
        uuid for uuid in BLE_REQUIRED_CHARACTERISTICS
        if services.get_characteristic(uuid) is None
    )


class BleAudioReassembler:
    def __init__(self) -> None:
        self._sequence: int | None = None
        self._count = 0
        self._parts: dict[int, bytes] = {}

    def feed(self, value: bytes) -> bytes | None:
        if len(value) <= BLE_AUDIO_FRAGMENT.size:
            return None
        magic, sequence, index, count = BLE_AUDIO_FRAGMENT.unpack_from(value)
        if magic != b"BA" or count == 0 or count > 32 or index >= count:
            return None
        if sequence != self._sequence or count != self._count:
            self._sequence = sequence
            self._count = count
            self._parts.clear()
        self._parts[index] = value[BLE_AUDIO_FRAGMENT.size :]
        if len(self._parts) != count:
            return None
        body = b"".join(self._parts[index] for index in range(count))
        self._parts.clear()
        if len(body) != BLE_AUDIO_BODY_SIZE:
            return None
        return body


class BleStreamWriter:
    """Minimal asyncio StreamWriter-compatible adapter over a GATT write."""

    def __init__(self, client: Any, address: str) -> None:
        self.client = client
        self.address = address
        self._pending = bytearray()
        self._write_lock = asyncio.Lock()
        self._closed = asyncio.Event()
        self._closing = False

    def write(self, data: bytes) -> None:
        if not self._closing:
            self._pending.extend(data)

    async def drain(self) -> None:
        async with self._write_lock:
            if not self._pending or self._closing:
                return
            characteristic = self.client.services.get_characteristic(BLE_CONTROL_RX_UUID)
            maximum = int(getattr(characteristic, "max_write_without_response_size", 20) or 20)
            maximum = max(20, min(244, maximum))
            while self._pending:
                chunk = bytes(self._pending[:maximum])
                del self._pending[:maximum]
                await self.client.write_gatt_char(
                    BLE_CONTROL_RX_UUID, chunk, response=False
                )

    def get_extra_info(self, name: str, default: object = None) -> object:
        if name == "peername":
            return (f"ble:{self.address}", 0)
        return default

    def is_closing(self) -> bool:
        return self._closing

    def close(self) -> None:
        if self._closing:
            return
        self._closing = True
        try:
            asyncio.get_running_loop().create_task(self._disconnect())
        except RuntimeError:
            self._closed.set()

    async def _disconnect(self) -> None:
        try:
            if self.client.is_connected:
                await self.client.disconnect()
        finally:
            self._closed.set()

    async def wait_closed(self) -> None:
        await self._closed.wait()

    def mark_disconnected(self) -> None:
        self._closing = True
        self._closed.set()


class BleBridgeTransport:
    def __init__(self, app: Any) -> None:
        self.app = app
        self.scanner: Any = None
        self.tasks: dict[str, asyncio.Task[None]] = {}
        self.clients: dict[str, Any] = {}
        self.writers: dict[str, BleStreamWriter] = {}
        self.readers: dict[str, asyncio.StreamReader] = {}
        self.audio: dict[BleStreamWriter, BleAudioReassembler] = {}
        self.retry_after: dict[str, float] = {}
        self.failures: dict[str, int] = {}
        self.gatt_repairs: dict[str, int] = {}
        self.last_error = ""
        self._stopping = False
        self._scanner_running = False
        self._connecting_address: str | None = None
        self._next_scanner_retry = 0.0

    async def start(self) -> None:
        if sys.platform != "win32":
            return
        self._stopping = False
        await self._start_scanner()

    async def _start_scanner(self) -> None:
        if self._stopping or self.scanner is not None:
            return
        try:
            from bleak import BleakScanner

            self.scanner = BleakScanner(
                self._detected, service_uuids=[BLE_SERVICE_UUID]
            )
            await self.scanner.start()
            self._scanner_running = True
            self._next_scanner_retry = 0.0
            self.last_error = ""
            LOG.info("Bluetooth scanner started for PanPal")
        except Exception as exc:
            self.scanner = None
            self._scanner_running = False
            self._next_scanner_retry = asyncio.get_running_loop().time() + 5.0
            self.last_error = str(exc)
            LOG.warning("Bluetooth scanner unavailable: %s", exc)

    async def ensure_scanning(self) -> None:
        """Recover scanning after Bluetooth was unavailable during login."""

        if (
            self._stopping
            or self._scanner_running
            or self.clients
            or self.tasks
            or self._connecting_address is not None
        ):
            return
        if asyncio.get_running_loop().time() < self._next_scanner_retry:
            return
        if self.scanner is None:
            await self._start_scanner()
        else:
            await self._resume_scanner()

    async def stop(self) -> None:
        self._stopping = True
        if self.scanner is not None:
            try:
                if self._scanner_running:
                    await self.scanner.stop()
            except Exception:
                LOG.debug("Bluetooth scanner stop failed", exc_info=True)
            self._scanner_running = False
            self.scanner = None
        for client in tuple(self.clients.values()):
            try:
                if client.is_connected:
                    await client.disconnect()
            except Exception:
                LOG.debug("Bluetooth disconnect failed", exc_info=True)
        for task in tuple(self.tasks.values()):
            task.cancel()
        self.tasks.clear()
        self.clients.clear()
        self.writers.clear()
        self.readers.clear()
        self.audio.clear()
        self.retry_after.clear()
        self.failures.clear()
        self.gatt_repairs.clear()
        self._connecting_address = None
        self._next_scanner_retry = 0.0

    def _detected(self, device: Any, advertisement: Any) -> None:
        address = str(device.address)
        # Most Windows adapters cannot reliably scan while pairing and reading
        # an uncached GATT database. Serialize the radio workflow around one
        # Cardputer instead of starting overlapping connection tasks.
        if self._connecting_address is not None:
            return
        if address in self.tasks or address in self.clients:
            return
        if asyncio.get_running_loop().time() < self.retry_after.get(address, 0.0):
            return
        service_uuids = {str(value).lower() for value in advertisement.service_uuids or []}
        if BLE_SERVICE_UUID not in service_uuids:
            return
        self._connecting_address = address
        task = asyncio.create_task(self._connect(device))
        self.tasks[address] = task
        task.add_done_callback(
            lambda _task, key=address: self._connection_task_done(key)
        )

    def _connection_task_done(self, address: str) -> None:
        self.tasks.pop(address, None)
        if self._connecting_address == address:
            self._connecting_address = None

    async def _pause_scanner(self) -> None:
        if self.scanner is None or not self._scanner_running:
            return
        await self.scanner.stop()
        self._scanner_running = False
        LOG.debug("Bluetooth scanner paused for GATT connection")

    async def _resume_scanner(self) -> None:
        if self._stopping or self.scanner is None or self._scanner_running:
            return
        try:
            await self.scanner.start()
            self._scanner_running = True
            self._next_scanner_retry = 0.0
            LOG.debug("Bluetooth scanner resumed")
        except Exception as exc:
            self._next_scanner_retry = asyncio.get_running_loop().time() + 5.0
            self.last_error = f"Bluetooth scanner could not resume: {exc}"
            LOG.warning("Bluetooth scanner could not resume: %s", exc)

    async def _connect(self, device: Any) -> None:
        from bleak import BleakClient

        address = str(device.address)
        reader = asyncio.StreamReader(limit=4097)
        writer: BleStreamWriter | None = None

        def disconnected(_client: Any) -> None:
            reader.feed_eof()
            if writer is not None:
                writer.mark_disconnected()

        client = BleakClient(
            device,
            disconnected_callback=disconnected,
            pair=True,
            timeout=20,
            # PanPal is a DIY peripheral whose GATT layout can change with
            # a firmware update. Windows otherwise tends to reuse an old GATT
            # cache and reports that a characteristic is missing even though
            # the current firmware exposes it.
            winrt={"use_cached_services": False},
        )
        retry_scheduled = False
        try:
            # Stop active discovery before pairing/service enumeration. This is
            # required for stable WinRT GATT discovery on common USB adapters.
            await self._pause_scanner()
            await client.connect()
            missing = missing_ble_characteristics(client.services)
            if missing:
                if await self._repair_stale_gatt_cache(client, address, missing):
                    retry_scheduled = True
                    return
                raise RuntimeError(
                    "Cardputer GATT database is still incomplete after automatic "
                    "Windows bond refresh; missing characteristic(s): "
                    + ", ".join(missing)
                )
            writer = BleStreamWriter(client, address)
            self.clients[address] = client
            self.writers[address] = writer
            self.readers[address] = reader
            self.audio[writer] = BleAudioReassembler()

            def control_notification(_characteristic: Any, data: bytearray) -> None:
                reader.feed_data(bytes(data))

            def audio_notification(_characteristic: Any, data: bytearray) -> None:
                self._receive_audio(writer, bytes(data))

            await client.start_notify(BLE_CONTROL_TX_UUID, control_notification)
            await client.start_notify(BLE_AUDIO_TX_UUID, audio_notification)
            self.failures.pop(address, None)
            self.retry_after.pop(address, None)
            self.gatt_repairs.pop(address, None)
            self.last_error = ""
            LOG.info("Bluetooth control connected: %s", address)
            await self.app.handle_client(reader, writer)
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            self.last_error = str(exc)
            LOG.warning("Bluetooth connection failed for %s: %s", address, exc)
            self._schedule_retry(address)
            retry_scheduled = True
        finally:
            self.clients.pop(address, None)
            self.writers.pop(address, None)
            self.readers.pop(address, None)
            if writer is not None:
                self.audio.pop(writer, None)
                writer.mark_disconnected()
            try:
                if client.is_connected:
                    await client.disconnect()
            except Exception:
                pass
            if not retry_scheduled and not self._stopping:
                self._schedule_retry(address)
            await self._resume_scanner()

    async def _repair_stale_gatt_cache(
        self, client: Any, address: str, missing: tuple[str, ...]
    ) -> bool:
        """Refresh a broken Windows bond once without losing app pairing.

        WinRT can return a partially cached GATT database even for an explicit
        uncached query. Recreating the Windows Just Works bond clears that
        cache. The Cardputer/Bridge application token is stored separately on
        both peers, so the following reconnect authenticates with ``hello_ok``
        and never asks the user for the six-digit application code again.
        """

        if self.gatt_repairs.get(address, 0) >= 1:
            return False
        self.gatt_repairs[address] = 1
        self.last_error = "正在自动刷新 Cardputer 蓝牙服务缓存…"
        self.app._status_changed()
        LOG.warning(
            "Refreshing stale Windows GATT bond for %s; missing: %s",
            address,
            ", ".join(missing),
        )
        try:
            # Bleak's WinRT unpair operation also closes the active session.
            # On the next advertisement pair=True silently creates a fresh
            # Just Works bond; no Windows Settings interaction is required.
            await client.unpair()
        except Exception as exc:
            self.last_error = f"自动刷新 Cardputer 蓝牙缓存失败: {exc}"
            LOG.warning("Automatic Windows GATT bond refresh failed: %s", exc)
            return False
        self.failures.pop(address, None)
        self.retry_after[address] = asyncio.get_running_loop().time() + 1.0
        LOG.info("Windows GATT bond removed; automatic re-pair scheduled")
        return True

    def _schedule_retry(self, address: str) -> None:
        if self._stopping:
            return
        failures = min(self.failures.get(address, 0) + 1, 5)
        self.failures[address] = failures
        delay = min(30.0, float(2 ** failures))
        self.retry_after[address] = asyncio.get_running_loop().time() + delay
        LOG.info("Bluetooth retry for %s in %.0f seconds", address, delay)

    def _receive_audio(self, writer: BleStreamWriter, value: bytes) -> None:
        reassembler = self.audio.get(writer)
        device = self.app.connected_devices.get(writer)
        if reassembler is None or device is None:
            return
        body = reassembler.feed(value)
        if body is None:
            return
        try:
            packet = unpack_ble_audio(device.token, body)
        except ProtocolError:
            return
        self.app.audio.feed(packet.sequence, packet.payload)
        device.touch()
        device.audio_packets += 1
