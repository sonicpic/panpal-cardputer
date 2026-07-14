#include "audio_tx.h"

#include <lwip/def.h>
#include <mbedtls/md.h>

#include <cstring>

namespace cardbridge {
namespace {

struct __attribute__((packed)) AudioPacketHeader {
  uint32_t sequence;
  uint32_t timestampMs;
  uint8_t hmac[8];
};

}  // namespace

bool AudioTransmitter::begin(bool muted) {
  muted_ = muted;
  queue_ = xQueueCreate(kAudioRingFrames, sizeof(AudioFrame));
  if (!queue_) return false;
  udp_.begin(0);
  const BaseType_t captureOk = xTaskCreatePinnedToCore(
      captureTaskEntry, "mic_capture", 4096, this, 3, &captureTask_, 0);
  const BaseType_t senderOk = xTaskCreatePinnedToCore(
      senderTaskEntry, "audio_udp", 4096, this, 2, &senderTask_, 0);
  return captureOk == pdPASS && senderOk == pdPASS;
}

void AudioTransmitter::setMuted(bool muted) {
  muted_ = muted;
  if (muted && queue_) xQueueReset(queue_);
}

bool AudioTransmitter::streamingAllowed() const {
  if (muted_) return false;
  IPAddress ignoredIp;
  uint8_t ignoredToken[32];
  return pairing_.audioEndpoint(ignoredIp, ignoredToken);
}

void AudioTransmitter::captureTaskEntry(void* argument) {
  static_cast<AudioTransmitter*>(argument)->captureLoop();
}

void AudioTransmitter::senderTaskEntry(void* argument) {
  static_cast<AudioTransmitter*>(argument)->senderLoop();
}

// The ADV microphone is driven directly: M5Unified's Mic_Class is broken for
// this board on both Arduino cores (all-zero samples on core 2.x; wrong frame
// pacing plus WiFi starvation on core 3.x — verified on hardware 2026-07-14),
// while the factory demo only works built against native ESP-IDF. We instead
// program the ES8311 ADC over I2C (the I2C path is proven by the speaker) and
// read PCM with the IDF i2s_std driver. ES8311 register sequences mirror
// M5Unified's `_microphone_enabled_cb_cardputer_adv`.
bool AudioTransmitter::micStart() {
  // Working recipe from vc1235-ui/vibe-cardputer (same board, same bug, solved).
  // Three non-obvious requirements, all mandatory:
  //   1. gpio_reset_pin() first — clear M5Unified's residual I2S pin state.
  //   2. MCLK routed to GPIO0 (physically unwired) — with I2S_GPIO_UNUSED the
  //      IDF 5.x clock tree doesn't fully init and DIN floats high (0xFFFF).
  //   3. Start I2S (BCLK running) BEFORE configuring the ES8311, so its PLL
  //      can lock onto BCLK. 16-bit STEREO slots → BCLK = 512kHz, ES8311
  //      pre_multi x8 → 4.096MHz internal MCLK, /256 → 16kHz.

  gpio_reset_pin(GPIO_NUM_41);
  gpio_reset_pin(GPIO_NUM_43);
  gpio_reset_pin(GPIO_NUM_46);

  i2s_chan_config_t channelConfig =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channelConfig.auto_clear = true;
  if (i2s_new_channel(&channelConfig, nullptr, &rxChannel_) != ESP_OK) {
    return false;
  }
  i2s_std_config_t standardConfig = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kAudioSampleRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = GPIO_NUM_0,  // unwired, but forces clock-tree init
          .bclk = GPIO_NUM_41,
          .ws = GPIO_NUM_43,
          .dout = I2S_GPIO_UNUSED,
          .din = GPIO_NUM_46,
          .invert_flags = {},
      },
  };
  if (i2s_channel_init_std_mode(rxChannel_, &standardConfig) != ESP_OK ||
      i2s_channel_enable(rxChannel_) != ESP_OK) {
    i2s_del_channel(rxChannel_);
    rxChannel_ = nullptr;
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(200));  // let BCLK stabilize before codec PLL lock

  // ES8311 register sequence ported verbatim from the working firmware.
  auto write = [](uint8_t reg, uint8_t value) {
    M5.In_I2C.writeRegister8(0x18, reg, value, 100000);
  };
  write(0x01, 0x30);
  write(0x02, 0x00);
  write(0x03, 0x10);
  write(0x16, 0x24);  // MIC gain
  write(0x04, 0x10);
  write(0x05, 0x00);
  write(0x0B, 0x00);
  write(0x0C, 0x00);
  write(0x10, 0x1F);
  write(0x11, 0x7F);
  write(0x00, 0x80);  // CSM power on, slave mode
  vTaskDelay(pdMS_TO_TICKS(20));
  write(0x01, 0xBF);  // clock source = BCLK, all clocks on
  // Clock coefficients: pre_div=1, pre_multi=x8, lrck_div=256 (BCLK 512k → 16k)
  write(0x02, 0x18);  // (0<<5) | (3<<3): pre_div-1=0, pre_multi=x8
  write(0x05, 0x00);
  write(0x03, 0x10);  // adc_osr
  write(0x04, 0x10);
  write(0x07, 0x00);
  write(0x08, 0xFF);  // lrck_l = 255
  write(0x06, 0x03);  // bclk_div-1
  write(0x09, 0x0C);  // SDP in: Philips, 16-bit
  write(0x0A, 0x0C);  // SDP out: 16-bit, ADC output enabled
  write(0x0D, 0x01);  // power up analog
  write(0x0E, 0x02);
  write(0x12, 0x00);
  write(0x13, 0x10);
  write(0x1B, 0x0A);  // HPF
  write(0x1C, 0x6A);  // DC offset cancel
  write(0x17, 0xFF);  // ADC digital volume: max — the analog MEMS mic is quiet;
                      // at 0xBF speech peaked ~350/32767. 0xFF lifts it into a
                      // healthy STT range (verified: clear speech, peak ~28k).
  write(0x15, 0x40);  // ADC ramp rate
  write(0x14, 0x1A);  // analog MIC, max analog PGA gain (nibble 0xA = +30dB)
  write(0x37, 0x48);
  write(0x44, 0x08);
  write(0x45, 0x00);
  vTaskDelay(pdMS_TO_TICKS(50));
  return true;
}

void AudioTransmitter::micStop() {
  if (rxChannel_) {
    i2s_channel_disable(rxChannel_);
    i2s_del_channel(rxChannel_);
    rxChannel_ = nullptr;
  }
  static constexpr uint8_t kDisableSeq[][2] = {
      {0x0D, 0xFC},  // SYSTEM: power down analog circuitry
      {0x0E, 0x6A},
      {0x00, 0x00},  // RESET: CSM power down
  };
  for (const auto& entry : kDisableSeq) {
    M5.In_I2C.writeRegister8(0x18, entry[0], entry[1], 100000);
  }
}

void AudioTransmitter::captureLoop() {
  uint32_t sequence = 0;
  for (;;) {
    if (!streamingAllowed()) {
      if (rxChannel_) {
        micStop();
        level_ = 0;
        rawPeak_ = 0;
        xQueueReset(queue_);
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!rxChannel_) {
      if (micStart()) {
        ++micBeginOk_;
      } else {
        ++micBeginFail_;
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
    }

    AudioFrame frame{};
    frame.sequence = sequence++;
    frame.timestampMs = millis();
    // Read interleaved 16-bit L/R stereo; keep the left slot (ES8311 ADC).
    static int16_t stereo[kAudioSamplesPerFrame * 2];
    size_t received = 0;
    bool readOk = true;
    while (received < sizeof(stereo)) {
      size_t bytesRead = 0;
      if (i2s_channel_read(rxChannel_,
                           reinterpret_cast<uint8_t*>(stereo) + received,
                           sizeof(stereo) - received, &bytesRead,
                           pdMS_TO_TICKS(200)) != ESP_OK) {
        readOk = false;
        break;
      }
      received += bytesRead;
    }
    if (!readOk) {
      ++recordFail_;
      continue;
    }
    for (size_t i = 0; i < kAudioSamplesPerFrame; ++i) {
      frame.samples[i] = stereo[i * 2];
    }
    for (size_t i = 0; i < 64; ++i) debugStereo_[i] = stereo[i];
    ++captured_;
    if (!streamingAllowed()) continue;

    uint16_t peak = 0;
    for (size_t i = 0; i < kAudioSamplesPerFrame; ++i) {
      int32_t sample = frame.samples[i];
      const uint16_t absolute = static_cast<uint16_t>(
          sample < 0 ? min<int32_t>(-sample, 32767) : sample);
      if (absolute > peak) peak = absolute;
    }
    rawPeak_ = peak;
    level_ = static_cast<uint8_t>(min<uint32_t>(255, peak >> 7));

    if (xQueueSend(queue_, &frame, 0) != pdPASS) {
      AudioFrame discarded;
      xQueueReceive(queue_, &discarded, 0);
      xQueueSend(queue_, &frame, 0);
      ++droppedFrames_;
      ++queueDrop_;
    }
  }
}

void AudioTransmitter::senderLoop() {
  AudioFrame frame;
  for (;;) {
    if (xQueueReceive(queue_, &frame, pdMS_TO_TICKS(100)) != pdPASS) continue;
    IPAddress ip;
    uint8_t token[32];
    if (muted_ || !pairing_.audioEndpoint(ip, token)) continue;
    if (sendFrame(frame, ip, token)) {
      ++sent_;
    } else {
      ++droppedFrames_;
      ++sendFail_;
    }
  }
}

bool AudioTransmitter::sendFrame(const AudioFrame& frame, const IPAddress& ip,
                                 const uint8_t token[32]) {
  AudioPacketHeader header{};
  header.sequence = htonl(frame.sequence);
  header.timestampMs = htonl(frame.timestampMs);

  uint8_t digest[32];
  mbedtls_md_context_t context;
  mbedtls_md_init(&context);
  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  bool ok = info && mbedtls_md_setup(&context, info, 1) == 0 &&
            mbedtls_md_hmac_starts(&context, token, 32) == 0 &&
            mbedtls_md_hmac_update(
                &context, reinterpret_cast<const uint8_t*>(&header), 8) == 0 &&
            mbedtls_md_hmac_update(
                &context, reinterpret_cast<const uint8_t*>(frame.samples),
                kAudioPayloadBytes) == 0 &&
            mbedtls_md_hmac_finish(&context, digest) == 0;
  mbedtls_md_free(&context);
  if (!ok) return false;
  memcpy(header.hmac, digest, sizeof(header.hmac));

  if (!udp_.beginPacket(ip, kAudioPort)) return false;
  udp_.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
  udp_.write(reinterpret_cast<const uint8_t*>(frame.samples), kAudioPayloadBytes);
  return udp_.endPacket() == 1;
}

}  // namespace cardbridge
