# Current architecture

```text
Cardputer ADV firmware (ESP32-S3)
  ├─ mDNS discovery: _cardbridge._tcp
  ├─ TCP 7788: authenticated JSON key/control protocol
  └─ UDP 7789: authenticated 16 kHz PCM16 audio frames
                 │ local network
                 ▼
PanPal (`CardBridge.app`, SwiftUI menu bar process)
  └─ supervised CardBridgeAgent (bundled Python runtime)
       ├─ Bonjour discovery and pairing
       ├─ Quartz keyboard injection
       ├─ audio jitter buffer → CardBridge Microphone Feed
       ├─ local owner-only Unix control socket
       └─ read-only Codex session/status integration
```

The user-facing `CardBridge Microphone` is an input-only Core Audio HAL device.
The Agent writes to its paired output-only Feed device. BlackHole 2ch remains a
compatibility fallback when the bundled driver is absent.

The App owns lifecycle, permission requests, login launch, updates, and
diagnostics. The Agent owns network/audio/device behavior. Pairing secrets are
kept outside ordinary status snapshots and logs.

## Trust boundaries

- The Cardputer and Mac must share a trusted local network.
- Pairing creates a random long-lived token; later device messages require it.
- UDP audio is authenticated but not retransmitted or encrypted separately.
- The local Unix socket is owner-only and validates the connecting UID.
- Codex integration exposes only short, privacy-trimmed public status.
