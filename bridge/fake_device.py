#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import socket
import struct
import time
from pathlib import Path
from typing import Any

from cardbridge._generated_version import (
    CAPABILITIES,
    DEVICE_PROTOCOL_MAJOR,
    DEVICE_PROTOCOL_MINOR,
    FIRMWARE_VERSION,
)
from cardbridge.protocol import AUDIO_SAMPLES_PER_FRAME, encode_message, pack_audio


class FakeDevice:
    def __init__(
        self,
        host: str,
        tcp_port: int,
        udp_port: int,
        device_id: str = "fakecardputer001",
        state_path: Path | None = None,
    ) -> None:
        self.host = host
        self.tcp_port = tcp_port
        self.udp_port = udp_port
        self.device_id = device_id
        self.state_path = state_path
        self.token = self._load_token()
        self.socket: socket.socket | None = None
        self.file: Any = None

    def _load_token(self) -> str | None:
        if not self.state_path or not self.state_path.exists():
            return None
        try:
            value = json.loads(self.state_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None
        token = value.get("token") if isinstance(value, dict) else None
        return token if isinstance(token, str) else None

    def _save_token(self) -> None:
        if not self.state_path or not self.token:
            return
        self.state_path.parent.mkdir(parents=True, exist_ok=True)
        self.state_path.write_text(json.dumps({"token": self.token}) + "\n", encoding="utf-8")

    def connect(self, pair_code: str | None = None) -> None:
        self.socket = socket.create_connection((self.host, self.tcp_port), timeout=5)
        self.file = self.socket.makefile("rwb", buffering=0)
        hello = {
            "t": "hello",
            "dev_id": self.device_id,
            "token": self.token,
            "device": {
                "model": "fake-cardputer",
                "firmware": FIRMWARE_VERSION,
                "build": "test",
            },
            "protocol": {
                "major": DEVICE_PROTOCOL_MAJOR,
                "minor": DEVICE_PROTOCOL_MINOR,
            },
            "capabilities": list(CAPABILITIES),
        }
        self.send(hello)
        response = self.receive()
        if response["t"] == "auth_error":
            self.token = None
            hello["token"] = None
            self.send(hello)
            response = self.receive()
        if response["t"] == "upgrade_required":
            raise RuntimeError(f"firmware update required: {response}")
        if response["t"] == "pair_required":
            if pair_code is None:
                pair_code = input("Pairing code shown by CardBridge: ").strip()
            self.send({"t": "pair", "code": pair_code})
            response = self.receive()
        if response["t"] == "pair_error":
            raise RuntimeError("pairing code was rejected")
        if response["t"] == "paired":
            self.token = str(response["token"])
            self._save_token()
        elif response["t"] != "hello_ok":
            raise RuntimeError(f"unexpected handshake response: {response}")
        if not self.token:
            raise RuntimeError("bridge authenticated without a token")
        print(f"Authenticated to {response.get('mac_name', 'Mac')}")

    def close(self) -> None:
        if self.file:
            self.file.close()
        if self.socket:
            self.socket.close()

    def send(self, message: dict[str, Any]) -> None:
        assert self.file is not None
        outgoing = dict(message)
        if self.token and outgoing.get("t") not in {"hello", "pair"}:
            outgoing.setdefault("token", self.token)
        self.file.write(encode_message(outgoing))

    def receive(self) -> dict[str, Any]:
        assert self.file is not None
        line = self.file.readline()
        if not line:
            raise ConnectionError("bridge closed the connection")
        return json.loads(line)

    def key(self, key: str, action: str, modifiers: list[str] | None = None) -> None:
        self.send({"t": "key", "k": key, "m": modifiers or [], "a": action})

    def type_text(self, text: str, interval: float = 0.015) -> None:
        shifted = {
            "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6",
            "&": "7", "*": "8", "(": "9", ")": "0", "_": "-", "+": "=",
            "{": "[", "}": "]", "|": "\\", ":": ";", '"': "'", "<": ",",
            ">": ".", "?": "/", "~": "`",
        }
        for character in text:
            if character == "\n":
                key, modifiers = "enter", []
            elif character.isalpha() and character.isupper():
                key, modifiers = character.lower(), ["shift"]
            elif character in shifted:
                key, modifiers = shifted[character], ["shift"]
            else:
                key, modifiers = character, []
            self.key(key, "down", modifiers)
            self.key(key, "up", modifiers)
            time.sleep(interval)

    def send_sine(self, seconds: float = 2.0, frequency: float = 440.0) -> None:
        assert self.token
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sequence = 0
        frame_count = int(seconds * 50)
        started = time.monotonic()
        for frame_index in range(frame_count):
            samples = []
            for sample_index in range(AUDIO_SAMPLES_PER_FRAME):
                offset = frame_index * AUDIO_SAMPLES_PER_FRAME + sample_index
                samples.append(int(8000 * math.sin(2 * math.pi * frequency * offset / 16_000)))
            payload = struct.pack("<320h", *samples)
            timestamp = int((time.monotonic() - started) * 1000)
            datagram = pack_audio(self.token, sequence, timestamp, payload)
            udp.sendto(datagram, (self.host, self.udp_port))
            sequence += 1
            deadline = started + sequence * 0.020
            time.sleep(max(0.0, deadline - time.monotonic()))
        udp.close()
        print(f"Sent {frame_count} authenticated 20 ms sine-wave frames")


def main() -> None:
    parser = argparse.ArgumentParser(description="Simulated Cardputer for bridge testing")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7788)
    parser.add_argument("--udp-port", type=int, default=7789)
    parser.add_argument("--pair-code")
    parser.add_argument("--state", type=Path, default=Path(".fake_device.json"))
    parser.add_argument("--text", default="Hello, Cardputer!\n")
    parser.add_argument("--audio-seconds", type=float, default=2.0)
    args = parser.parse_args()

    device = FakeDevice(args.host, args.port, args.udp_port, state_path=args.state)
    try:
        device.connect(args.pair_code)
        device.type_text(args.text)
        device.key("f13", "down")
        time.sleep(0.35)
        device.key("f13", "up")
        device.send({"t": "agent_list_req"})  # Reserved type must be harmless.
        device.send_sine(args.audio_seconds)
    finally:
        device.close()


if __name__ == "__main__":
    main()
