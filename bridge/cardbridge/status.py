from __future__ import annotations

import time
from dataclasses import dataclass, field

from .versioning import DeviceCompatibility


def now_ms() -> int:
    return int(time.time() * 1000)


@dataclass
class ConnectedDevice:
    device_id: str
    peer_ip: str
    token: str = field(repr=False)
    compatibility: DeviceCompatibility = field(repr=False)
    connected_at_ms: int = field(default_factory=now_ms)
    last_seen_ms: int = field(default_factory=now_ms)
    audio_packets: int = 0

    def touch(self) -> None:
        self.last_seen_ms = now_ms()

    def snapshot(self) -> dict[str, object]:
        return {
            "id": self.device_id,
            "ip": self.peer_ip,
            "model": self.compatibility.model or "cardputer",
            "firmware": self.compatibility.firmware_version or "unknown",
            "firmware_build": self.compatibility.firmware_build,
            "protocol": {
                "major": self.compatibility.protocol_major,
                "minor": self.compatibility.negotiated_minor,
            },
            "compatibility": "legacy" if self.compatibility.legacy else "ok",
            "capabilities": list(self.compatibility.capabilities),
            "connected_at_ms": self.connected_at_ms,
            "last_seen_ms": self.last_seen_ms,
            "audio_packets": self.audio_packets,
        }
