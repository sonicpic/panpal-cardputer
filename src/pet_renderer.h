#pragma once

#include <M5Cardputer.h>

#include "pet_assets.h"

namespace cardbridge {

enum class PetVisualState : uint8_t {
  Offline,
  Idle,
  Running,
  NeedsInput,
  Ready,
  Blocked,
};

class PetRenderer {
 public:
  void draw(M5Canvas& canvas, PetVisualState state, int x, int y,
            uint32_t nowMs);

 private:
  pet_assets::AnimationId animationFor(PetVisualState state) const;
  void drawFrame(M5Canvas& canvas, const pet_assets::Frame& frame,
                 int x, int y) const;

  PetVisualState state_ = PetVisualState::Offline;
  uint8_t frameIndex_ = 0;
  uint32_t frameStartedMs_ = 0;
};

}  // namespace cardbridge
