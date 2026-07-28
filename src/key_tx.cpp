#include "key_tx.h"

#include <cstring>

#include "keymap.h"

namespace cardbridge {
namespace {

constexpr uint32_t kKeyAckTimeoutMs = 300;
constexpr uint8_t kKeyMaxAttempts = 5;

void copyKey(char* destination, size_t capacity, const char* source) {
  if (!source || capacity == 0) return;
  strlcpy(destination, source, capacity);
}

}  // namespace

void KeyTransmitter::tick(bool uiConsumesKeyboard) {
  if (!pairing_.connected()) {
    previousCount_ = 0;
    clearPending();
    seenAckRevision_ = pairing_.keyAckRevision();
    return;
  }

  processPending();
  if (uiConsumesKeyboard) {
    releaseAll();
    processPending();
    return;
  }

  ActiveKey current[6];
  const size_t currentCount = buildCurrent(current, 6);
  for (size_t i = 0; i < previousCount_; ++i) {
    if (!contains(current, currentCount, previous_[i].key)) send(previous_[i], "up");
  }
  for (size_t i = 0; i < currentCount; ++i) {
    if (!contains(previous_, previousCount_, current[i].key)) send(current[i], "down");
  }
  memcpy(previous_, current, sizeof(ActiveKey) * currentCount);
  previousCount_ = currentCount;
  processPending();
}

size_t KeyTransmitter::buildCurrent(ActiveKey* output, size_t capacity) {
  const auto& state = M5Cardputer.Keyboard.keysState();
  bool cmd = state.alt;
  bool shift = state.shift;
  bool option = state.opt;
  bool control = state.ctrl;
  size_t count = 0;

  // Shift travels only as a flag on the actual key event because a standalone
  // Shift event can toggle Chinese/English input before the intended key.
  // Ctrl/Cmd/Option retain real down/up events so they also work on their own
  // and across multi-key shortcuts.
  const auto& positions = M5Cardputer.Keyboard.keyList();
  for (const auto& position : positions) {
    const uint8_t physical =
        M5Cardputer.Keyboard.getKeyValue(position).value_first;
    if (!isKeyboardModifier(physical) || physical == KEY_LEFT_SHIFT ||
        count >= capacity) {
      continue;
    }
    const char* mapped = mapModifier(physical);
    if (!mapped) continue;
    copyKey(output[count].key, sizeof(output[count].key), mapped);
    output[count].physical = static_cast<uint8_t>(position.y * 14 + position.x);
    output[count].cmd = cmd;
    output[count].shift = shift;
    output[count].option = option;
    output[count].control = control;
    ++count;
  }

  for (const auto& position : positions) {
    const uint8_t physical =
        M5Cardputer.Keyboard.getKeyValue(position).value_first;
    if (physical == KEY_FN || isKeyboardModifier(physical) || count >= capacity) continue;

    const uint8_t physicalId = static_cast<uint8_t>(position.y * 14 + position.x);
    const ActiveKey* latched = previousPhysical(physicalId);
    const char* mapped = state.fn
        ? mapFnKey(physical, settings_.typelessFunctionKey)
        : mapSpecialKey(physical);
    if (state.fn && mapped) {
      // A TCA8418 chord arrives as individual events. If the printable key
      // was observed just before Fn, prefer the now-complete Fn chord instead
      // of keeping the old printable mapping (notably Fn+` -> Escape).
      copyKey(output[count].key, sizeof(output[count].key), mapped);
    } else if (latched) {
      // Keep the logical key until this physical switch is released. In
      // particular, releasing Fn before Space must release F13, not type Space.
      copyKey(output[count].key, sizeof(output[count].key), latched->key);
    } else if (state.fn && !mapped) {
      continue;  // Fn combinations are explicit only.
    } else if (mapped) {
      copyKey(output[count].key, sizeof(output[count].key), mapped);
    } else if (physical >= 32 && physical <= 126) {
      output[count].key[0] = static_cast<char>(physical);
      output[count].key[1] = '\0';
    } else {
      continue;
    }
    output[count].physical = physicalId;
    output[count].cmd = cmd;
    output[count].shift = shift;
    output[count].option = option;
    output[count].control = control;
    ++count;
  }
  return count;
}

const KeyTransmitter::ActiveKey* KeyTransmitter::previousPhysical(
    uint8_t physical) const {
  for (size_t i = 0; i < previousCount_; ++i) {
    if (previous_[i].physical == physical) return &previous_[i];
  }
  return nullptr;
}

bool KeyTransmitter::contains(const ActiveKey* list, size_t count,
                              const char* key) const {
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(list[i].key, key) == 0) return true;
  }
  return false;
}

void KeyTransmitter::send(const ActiveKey& key, const char* action) {
  bool cmd = key.cmd;
  bool shift = key.shift;
  bool option = key.option;
  bool control = key.control;
  if (strcmp(action, "up") == 0) {
    if (strcmp(key.key, "cmd") == 0) cmd = false;
    if (strcmp(key.key, "alt") == 0) option = false;
    if (strcmp(key.key, "ctrl") == 0) control = false;
  }
  if (pendingCount_ >= kPendingCapacity) {
    Serial.printf("[keys] queue full; dropped %s %s\n", key.key, action);
    return;
  }
  const size_t tail = (pendingHead_ + pendingCount_) % kPendingCapacity;
  PendingKey& pending = pending_[tail];
  pending.key = key;
  pending.key.cmd = cmd;
  pending.key.shift = shift;
  pending.key.option = option;
  pending.key.control = control;
  pending.down = strcmp(action, "down") == 0;
  if (++nextRequestId_ == 0) ++nextRequestId_;
  pending.requestId = nextRequestId_;
  pending.sentMs = 0;
  pending.attempts = 0;
  ++pendingCount_;
  if (pending.down) ++sentKeys_;
}

void KeyTransmitter::releaseAll() {
  for (size_t i = 0; i < previousCount_; ++i) send(previous_[i], "up");
  previousCount_ = 0;
}

void KeyTransmitter::processPending() {
  if (!pendingCount_) return;
  PendingKey& pending = pending_[pendingHead_];
  const uint32_t revision = pairing_.keyAckRevision();
  if (revision != seenAckRevision_) {
    seenAckRevision_ = revision;
    if (pairing_.keyAckRequestId() == pending.requestId) {
      if (!pairing_.keyAckOk()) {
        Serial.printf("[keys] request=%lu rejected: %s\n",
                      static_cast<unsigned long>(pending.requestId),
                      pairing_.keyAckError().c_str());
      }
      popPending();
      if (!pendingCount_) return;
    }
  }

  PendingKey& current = pending_[pendingHead_];
  if (current.attempts == 0 || millis() - current.sentMs >= kKeyAckTimeoutMs) {
    if (current.attempts >= kKeyMaxAttempts) {
      Serial.printf("[keys] request=%lu timed out after %u attempts\n",
                    static_cast<unsigned long>(current.requestId),
                    current.attempts);
      popPending();
      if (!pendingCount_) return;
    }
    transmitPending();
  }
}

void KeyTransmitter::transmitPending() {
  if (!pendingCount_) return;
  PendingKey& pending = pending_[pendingHead_];
  const char* action = pending.down ? "down" : "up";
  const bool queued = pairing_.sendKey(
      pending.key.key, action, pending.key.cmd, pending.key.shift,
      pending.key.option, pending.key.control, pending.requestId);
  ++pending.attempts;
  pending.sentMs = millis();
  Serial.printf("[keys] %s %s request=%lu attempt=%u queued=%d\n",
                pending.key.key, action,
                static_cast<unsigned long>(pending.requestId),
                pending.attempts, queued);
}

void KeyTransmitter::popPending() {
  if (!pendingCount_) return;
  pendingHead_ = (pendingHead_ + 1) % kPendingCapacity;
  --pendingCount_;
}

void KeyTransmitter::clearPending() {
  pendingHead_ = 0;
  pendingCount_ = 0;
}

}  // namespace cardbridge
