#pragma once

#include <Arduino.h>

namespace cardbridge {

constexpr uint16_t kControlPort = 7788;
constexpr uint16_t kAudioPort = 7789;
constexpr uint32_t kAudioSampleRate = 16000;
constexpr size_t kAudioSamplesPerFrame = 320;  // 20 ms, mono, signed 16-bit.
constexpr size_t kAudioPayloadBytes = kAudioSamplesPerFrame * sizeof(int16_t);
constexpr size_t kAudioRingFrames = 8;
constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint8_t kHeartbeatMissLimit = 3;
constexpr uint32_t kReconnectMinMs = 1000;
// Leaves time for mDNS and TCP setup while meeting the 30 second recovery gate.
constexpr uint32_t kReconnectMaxMs = 20000;
constexpr size_t kMaxWifiNetworks = 8;
constexpr size_t kMaxPairedMacs = 4;
constexpr size_t kMaxDiscoveredMacs = 8;
constexpr uint8_t kDefaultBrightness = 128;
constexpr uint16_t kDefaultScreenTimeoutSec = 60;

}  // namespace cardbridge
