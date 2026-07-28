#pragma once

#include <M5Cardputer.h>

#include "audio_tx.h"
#include "pairing.h"

namespace cardbridge {

// Owns the physical push-to-talk gesture. G0 supports hold/release and a
// double-click latch; Fn+Space is an immediate hold/release alternative.
class VoiceController {
 public:
  VoiceController(PairingManager& pairing, AudioTransmitter& audio,
                  DeviceSettings& settings)
      : pairing_(pairing), audio_(audio), settings_(settings) {}

  void begin();
  void tick();
  bool active() const { return active_; }
  bool locked() const { return locked_; }
  bool consumesKeyboard() const { return keyboardCapture_; }

 private:
  enum class PendingAction : uint8_t {
    None,
    Down,
    Up,
  };

  bool spacePressed() const;
  void start(bool locked = false);
  void stop();
  void updateAggregate();
  void queueAction(PendingAction action);
  void transmitPending();
  void processAcknowledgement();
  bool requested() const;
  void resetLocal();

  PairingManager& pairing_;
  AudioTransmitter& audio_;
  DeviceSettings& settings_;
  bool active_ = false;
  bool locked_ = false;
  bool g0Held_ = false;
  bool fnSpaceHeld_ = false;
  bool keyboardCapture_ = false;
  bool pendingG0Release_ = false;
  bool ignoreG0Release_ = false;
  PendingAction pendingAction_ = PendingAction::None;
  uint8_t pendingAttempts_ = 0;
  uint32_t pendingRequestId_ = 0;
  uint32_t pendingSentMs_ = 0;
  uint32_t nextRequestId_ = 0;
  uint32_t seenAckRevision_ = 0;
  uint32_t g0PressedMs_ = 0;
  uint32_t g0ReleasedMs_ = 0;
};

}  // namespace cardbridge
