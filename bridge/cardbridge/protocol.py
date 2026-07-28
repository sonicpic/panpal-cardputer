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


@dataclass(frozen=True)
class BleAudioPacket:
    stream_id: int
    sequence: int
    timestamp_ms: int
    payload: bytes


BLE_AUDIO_FRAGMENT = struct.Struct("!2sIBB")
BLE_AUDIO_BODY_HEADER = struct.Struct("!IIIhBB")
BLE_ADPCM_BYTES = 160
BLE_AUDIO_HMAC_BYTES = 8
BLE_AUDIO_BODY_SIZE = BLE_AUDIO_BODY_HEADER.size + BLE_ADPCM_BYTES + BLE_AUDIO_HMAC_BYTES

_IMA_INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)
_IMA_STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
)


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


def decode_ima_adpcm(predictor: int, index: int, encoded: bytes) -> bytes:
    if len(encoded) != BLE_ADPCM_BYTES or not 0 <= index <= 88:
        raise ProtocolError("invalid IMA ADPCM frame")
    samples = [predictor]
    for byte in encoded:
        for code in (byte & 0x0F, byte >> 4):
            if len(samples) >= AUDIO_SAMPLES_PER_FRAME:
                break
            step = _IMA_STEP_TABLE[index]
            delta = step >> 3
            if code & 4:
                delta += step
            if code & 2:
                delta += step >> 1
            if code & 1:
                delta += step >> 2
            predictor += -delta if code & 8 else delta
            predictor = max(-32768, min(32767, predictor))
            index = max(0, min(88, index + _IMA_INDEX_TABLE[code]))
            samples.append(predictor)
    if len(samples) != AUDIO_SAMPLES_PER_FRAME:
        raise ProtocolError("IMA ADPCM frame did not decode to 320 samples")
    return struct.pack("<320h", *samples)


def unpack_ble_audio(token: str, body: bytes) -> BleAudioPacket:
    if len(body) != BLE_AUDIO_BODY_SIZE:
        raise ProtocolError(f"BLE audio body must be {BLE_AUDIO_BODY_SIZE} bytes")
    authenticated = body[:-BLE_AUDIO_HMAC_BYTES]
    signature = body[-BLE_AUDIO_HMAC_BYTES:]
    expected = hmac.new(token_bytes(token), authenticated, hashlib.sha256).digest()[:8]
    if not hmac.compare_digest(signature, expected):
        raise ProtocolError("BLE audio HMAC mismatch")
    stream_id, sequence, timestamp_ms, predictor, index, _reserved = (
        BLE_AUDIO_BODY_HEADER.unpack_from(body)
    )
    encoded = body[BLE_AUDIO_BODY_HEADER.size : -BLE_AUDIO_HMAC_BYTES]
    return BleAudioPacket(
        stream_id,
        sequence,
        timestamp_ms,
        decode_ima_adpcm(predictor, index, encoded),
    )


def encode_message(message: dict[str, Any]) -> bytes:
    # UTF-8 is both what ArduinoJson expects and materially smaller than six
    # byte ``\\uXXXX`` escapes for CJK session titles on the 4 KiB control
    # channel.
    encoded = (
        json.dumps(message, separators=(",", ":"), ensure_ascii=False) + "\n"
    ).encode("utf-8")
    if len(encoded) > MAX_JSON_LINE:
        raise ProtocolError("JSON line is too large")
    return encoded


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
