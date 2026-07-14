#pragma once

#include <M5Cardputer.h>
#include <WiFiUdp.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "app_config.h"
#include "pairing.h"

namespace cardbridge {

class AudioTransmitter {
 public:
  explicit AudioTransmitter(PairingManager& pairing) : pairing_(pairing) {}

  bool begin(bool muted);
  void setMuted(bool muted);
  bool muted() const { return muted_; }
  uint8_t level() const { return level_; }
  uint32_t droppedFrames() const { return droppedFrames_; }
  void dumpRaw() const {
    Serial.print("[raw] L:");
    for (size_t i = 0; i < 16; ++i) Serial.printf(" %04X", (uint16_t)debugStereo_[i * 2]);
    Serial.print("\n[raw] R:");
    for (size_t i = 0; i < 16; ++i) Serial.printf(" %04X", (uint16_t)debugStereo_[i * 2 + 1]);
    Serial.println();
  }
  void printDebug() const {
    Serial.printf(
        "[audio] mic_begin_ok=%u mic_begin_fail=%u captured=%u record_fail=%u "
        "sent=%u send_fail=%u queue_drop=%u level=%u raw_peak=%u\n",
        micBeginOk_, micBeginFail_, captured_, recordFail_, sent_, sendFail_,
        queueDrop_, level_, rawPeak_);
  }

 private:
  struct AudioFrame {
    uint32_t sequence;
    uint32_t timestampMs;
    int16_t samples[kAudioSamplesPerFrame];
  };

  static void captureTaskEntry(void* argument);
  static void senderTaskEntry(void* argument);
  void captureLoop();
  void senderLoop();
  bool streamingAllowed() const;
  bool micStart();
  void micStop();
  bool sendFrame(const AudioFrame& frame, const IPAddress& ip,
                 const uint8_t token[32]);

  PairingManager& pairing_;
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t captureTask_ = nullptr;
  TaskHandle_t senderTask_ = nullptr;
  i2s_chan_handle_t rxChannel_ = nullptr;
  WiFiUDP udp_;
  volatile bool muted_ = false;
  volatile uint8_t level_ = 0;
  volatile uint16_t rawPeak_ = 0;
  volatile uint32_t droppedFrames_ = 0;
  volatile uint32_t micBeginOk_ = 0;
  volatile uint32_t micBeginFail_ = 0;
  volatile uint32_t captured_ = 0;
  volatile uint32_t recordFail_ = 0;
  volatile uint32_t sent_ = 0;
  volatile uint32_t sendFail_ = 0;
  volatile uint32_t queueDrop_ = 0;
  volatile int16_t debugStereo_[64] = {};
};

}  // namespace cardbridge
