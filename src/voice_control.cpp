#include "voice_control.h"

namespace cardbridge {
namespace {

constexpr uint32_t kDoubleClickMs = 350;
constexpr uint32_t kHoldReleaseMs = 450;
constexpr uint32_t kVoiceAckTimeoutMs = 400;
constexpr uint8_t kVoiceMaxAttempts = 5;

}  // namespace

void VoiceController::begin() {
  audio_.setActive(false);
  seenAckRevision_ = pairing_.voiceAckRevision();
}

bool VoiceController::spacePressed() const {
  const auto& state = M5Cardputer.Keyboard.keysState();
  for (char character : state.word) {
    if (character == ' ') return true;
  }
  return false;
}

void VoiceController::tick() {
  const uint32_t now = millis();
  processAcknowledgement();

  if (pendingAction_ != PendingAction::None &&
      now - pendingSentMs_ >= kVoiceAckTimeoutMs) {
    if (pendingAttempts_ < kVoiceMaxAttempts && pairing_.connected()) {
      transmitPending();
    } else {
      Serial.printf("[voice] %s acknowledgement timed out after %u attempts\n",
                    pendingAction_ == PendingAction::Down ? "start" : "stop",
                    pendingAttempts_);
      pendingAction_ = PendingAction::None;
      active_ = false;
      audio_.setActive(false);
      if (!requested()) locked_ = false;
    }
  }

  // A lost authenticated session must fail closed: stop capture locally even
  // though an up edge can no longer be delivered to the old connection.
  if (!pairing_.connected() &&
      (active_ || pendingAction_ != PendingAction::None)) {
    g0Held_ = false;
    fnSpaceHeld_ = false;
    locked_ = false;
    pendingG0Release_ = false;
    resetLocal();
  }

  if (M5Cardputer.BtnA.wasPressed()) {
    if (locked_) {
      // One press ends a latched session. Its matching release must not start
      // another short push-to-talk session.
      locked_ = false;
      g0Held_ = false;
      pendingG0Release_ = false;
      ignoreG0Release_ = true;
      updateAggregate();
    } else if (pendingG0Release_ && now - g0ReleasedMs_ <= kDoubleClickMs) {
      pendingG0Release_ = false;
      g0Held_ = false;
      locked_ = true;
      ignoreG0Release_ = true;
      updateAggregate();
      pairing_.sendVoice("lock", true, 0);
      Serial.println("[voice] G0 double-click locked");
    } else {
      g0Held_ = true;
      g0PressedMs_ = now;
      ignoreG0Release_ = false;
      updateAggregate();
    }
  }

  if (M5Cardputer.BtnA.wasReleased()) {
    if (ignoreG0Release_) {
      ignoreG0Release_ = false;
    } else if (g0Held_) {
      g0Held_ = false;
      if (now - g0PressedMs_ >= kHoldReleaseMs) {
        pendingG0Release_ = false;
        updateAggregate();
      } else {
        // Keep the session alive briefly so a second click can latch without
        // producing an up/down shortcut glitch on Windows.
        pendingG0Release_ = true;
        g0ReleasedMs_ = now;
      }
    }
  }

  if (pendingG0Release_ && now - g0ReleasedMs_ > kDoubleClickMs) {
    pendingG0Release_ = false;
    updateAggregate();
  }

  const auto& state = M5Cardputer.Keyboard.keysState();
  const bool chord = state.fn && spacePressed();
  if (chord && !fnSpaceHeld_) {
    fnSpaceHeld_ = true;
    keyboardCapture_ = true;
    updateAggregate();
  } else if (!chord && fnSpaceHeld_) {
    fnSpaceHeld_ = false;
    updateAggregate();
  }
  if (keyboardCapture_ && !M5Cardputer.Keyboard.isPressed()) {
    keyboardCapture_ = false;
  }
}

void VoiceController::start(bool locked) {
  if (active_ || pendingAction_ == PendingAction::Down ||
      !pairing_.connected()) return;
  locked_ = locked_ || locked;
  queueAction(PendingAction::Down);
  Serial.printf("[voice] start requested locked=%d request=%lu\n", locked_,
                static_cast<unsigned long>(pendingRequestId_));
}

void VoiceController::stop() {
  if (!active_ && pendingAction_ == PendingAction::None) {
    audio_.setActive(false);
    return;
  }
  audio_.setActive(false);
  active_ = false;
  if (pairing_.connected()) {
    queueAction(PendingAction::Up);
    Serial.printf("[voice] stop requested request=%lu\n",
                  static_cast<unsigned long>(pendingRequestId_));
  } else {
    pendingAction_ = PendingAction::None;
  }
  locked_ = false;
}

void VoiceController::updateAggregate() {
  if (requested()) {
    start(locked_);
  } else {
    stop();
  }
}

bool VoiceController::requested() const {
  return g0Held_ || fnSpaceHeld_ || locked_ || pendingG0Release_;
}

void VoiceController::queueAction(PendingAction action) {
  pendingAction_ = action;
  pendingAttempts_ = 0;
  if (++nextRequestId_ == 0) ++nextRequestId_;
  pendingRequestId_ = nextRequestId_;
  transmitPending();
}

void VoiceController::transmitPending() {
  if (pendingAction_ == PendingAction::None) return;
  const char* action = pendingAction_ == PendingAction::Down ? "down" : "up";
  const bool sendEnter = pendingAction_ == PendingAction::Up &&
                         settings_.sendEnterAfterVoice;
  const bool queued = pairing_.sendVoice(action, locked_, pendingRequestId_,
                                         sendEnter);
  ++pendingAttempts_;
  pendingSentMs_ = millis();
  Serial.printf("[voice] %s tx request=%lu attempt=%u queued=%d enter=%d\n", action,
                static_cast<unsigned long>(pendingRequestId_), pendingAttempts_,
                queued, sendEnter);
}

void VoiceController::processAcknowledgement() {
  const uint32_t revision = pairing_.voiceAckRevision();
  if (revision == seenAckRevision_) return;
  seenAckRevision_ = revision;
  if (pendingAction_ == PendingAction::None ||
      pairing_.voiceAckRequestId() != pendingRequestId_) {
    return;
  }

  const bool wasDown = pendingAction_ == PendingAction::Down;
  if (!pairing_.voiceAckOk()) {
    Serial.printf("[voice] %s rejected by Bridge: %s\n",
                  pairing_.voiceAckAction().c_str(),
                  pairing_.voiceAckError().c_str());
    pendingAction_ = PendingAction::None;
    active_ = false;
    audio_.setActive(false);
    if (wasDown) locked_ = false;
    return;
  }

  pendingAction_ = PendingAction::None;
  if (wasDown) {
    if (requested()) {
      active_ = true;
      audio_.setActive(true);
      Serial.printf("[voice] start confirmed locked=%d\n", locked_);
      if (locked_) pairing_.sendVoice("lock", true, 0);
    } else {
      queueAction(PendingAction::Up);
    }
  } else {
    active_ = false;
    audio_.setActive(false);
    Serial.println("[voice] stop confirmed");
  }
}

void VoiceController::resetLocal() {
  pendingAction_ = PendingAction::None;
  pendingAttempts_ = 0;
  active_ = false;
  locked_ = false;
  audio_.setActive(false);
}

}  // namespace cardbridge
