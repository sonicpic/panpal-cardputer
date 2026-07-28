#pragma once

#include <M5Cardputer.h>

#include "models.h"
#include "pairing.h"

namespace cardbridge {

class KeyTransmitter {
 public:
  KeyTransmitter(PairingManager& pairing, const DeviceSettings& settings)
      : pairing_(pairing), settings_(settings) {}

  void tick(bool uiConsumesKeyboard);
  void releaseAll();
  uint32_t sentKeys() const { return sentKeys_; }

 private:
  struct ActiveKey {
    char key[20]{};
    uint8_t physical = 0xFF;
    bool cmd = false;
    bool shift = false;
    bool option = false;
    bool control = false;
  };

  struct PendingKey {
    ActiveKey key;
    bool down = false;
    uint32_t requestId = 0;
    uint32_t sentMs = 0;
    uint8_t attempts = 0;
  };

  size_t buildCurrent(ActiveKey* output, size_t capacity);
  bool contains(const ActiveKey* list, size_t count, const char* key) const;
  const ActiveKey* previousPhysical(uint8_t physical) const;
  void send(const ActiveKey& key, const char* action);
  void processPending();
  void transmitPending();
  void popPending();
  void clearPending();

  PairingManager& pairing_;
  const DeviceSettings& settings_;
  ActiveKey previous_[6];
  size_t previousCount_ = 0;
  static constexpr size_t kPendingCapacity = 16;
  PendingKey pending_[kPendingCapacity];
  size_t pendingHead_ = 0;
  size_t pendingCount_ = 0;
  uint32_t nextRequestId_ = 0;
  uint32_t seenAckRevision_ = 0;
  uint32_t sentKeys_ = 0;
};

}  // namespace cardbridge
