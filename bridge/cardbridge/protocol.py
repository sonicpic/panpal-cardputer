from __future__ import annotations

import hashlib
import hmac
import json
import struct
from dataclasses import dataclass
from typing import Any

AUDIO_SAMPLE_RATE = 16_000
AUDIO_SAMPLES_PER_FRAME = 320
AUDIO_PAYLOAD_SIZE = AUDIO_SAMPLES_PER_FRAME * 2
AUDIO_HEADER = struct.Struct("!II8s")
AUDIO_PACKET_SIZE = AUDIO_HEADER.size + AUDIO_PAYLOAD_SIZE
MAX_JSON_LINE = 4096


class ProtocolError(ValueError):
    pass


@dataclass(frozen=True)
class AudioPacket:
    sequence: int
    timestamp_ms: int
    payload: bytes


def token_bytes(token: str) -> bytes:
    if len(token) != 64:
        raise ProtocolError("token must encode exactly 32 random bytes")
    try:
        return bytes.fromhex(token)
    except ValueError as exc:
        raise ProtocolError("token is not hexadecimal") from exc


def audio_hmac(token: str, sequence: int, timestamp_ms: int, payload: bytes) -> bytes:
    if len(payload) != AUDIO_PAYLOAD_SIZE:
        raise ProtocolError(f"audio payload must be {AUDIO_PAYLOAD_SIZE} bytes")
    authenticated = struct.pack("!II", sequence & 0xFFFFFFFF, timestamp_ms & 0xFFFFFFFF) + payload
    return hmac.new(token_bytes(token), authenticated, hashlib.sha256).digest()[:8]


def pack_audio(token: str, sequence: int, timestamp_ms: int, payload: bytes) -> bytes:
    signature = audio_hmac(token, sequence, timestamp_ms, payload)
    return AUDIO_HEADER.pack(sequence & 0xFFFFFFFF, timestamp_ms & 0xFFFFFFFF, signature) + payload


def unpack_audio(token: str, datagram: bytes) -> AudioPacket:
    if len(datagram) != AUDIO_PACKET_SIZE:
        raise ProtocolError(f"audio datagram must be {AUDIO_PACKET_SIZE} bytes")
    sequence, timestamp_ms, signature = AUDIO_HEADER.unpack_from(datagram)
    payload = datagram[AUDIO_HEADER.size :]
    expected = audio_hmac(token, sequence, timestamp_ms, payload)
    if not hmac.compare_digest(signature, expected):
        raise ProtocolError("audio HMAC mismatch")
    return AudioPacket(sequence, timestamp_ms, payload)


def encode_message(message: dict[str, Any]) -> bytes:
    return (json.dumps(message, separators=(",", ":"), ensure_ascii=True) + "\n").encode("utf-8")


def decode_message(line: bytes) -> dict[str, Any]:
    if len(line) > MAX_JSON_LINE:
        raise ProtocolError("JSON line is too large")
    try:
        value = json.loads(line)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ProtocolError("invalid JSON line") from exc
    if not isinstance(value, dict) or not isinstance(value.get("t"), str):
        raise ProtocolError("message must be an object with a string 't'")
    return value
