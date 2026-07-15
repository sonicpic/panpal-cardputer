#include "pet_renderer.h"

namespace cardbridge {

pet_assets::AnimationId PetRenderer::animationFor(PetVisualState state) const {
  switch (state) {
    case PetVisualState::Running: return pet_assets::AnimationId::Running;
    case PetVisualState::NeedsInput: return pet_assets::AnimationId::Waiting;
    case PetVisualState::Ready: return pet_assets::AnimationId::Ready;
    case PetVisualState::Blocked: return pet_assets::AnimationId::Blocked;
    case PetVisualState::Offline:
    case PetVisualState::Idle: return pet_assets::AnimationId::Idle;
  }
  return pet_assets::AnimationId::Idle;
}

void PetRenderer::draw(M5Canvas& canvas, PetVisualState state, int x, int y,
                       uint32_t nowMs) {
  if (state != state_) {
    state_ = state;
    frameIndex_ = 0;
    frameStartedMs_ = nowMs;
  }
  const pet_assets::Animation& animation = pet_assets::get(animationFor(state));
  const pet_assets::Frame& current = animation.frames[frameIndex_];
  if (nowMs - frameStartedMs_ >= current.durationMs) {
    frameIndex_ = (frameIndex_ + 1) % animation.count;
    frameStartedMs_ = nowMs;
  }
  drawFrame(canvas, animation.frames[frameIndex_], x, y);
  if (state == PetVisualState::Offline) {
    canvas.drawLine(x + 11, y + 61, x + 61, y + 11, 0x8410);
  }
}

void PetRenderer::drawFrame(M5Canvas& canvas, const pet_assets::Frame& frame,
                            int x, int y) const {
  uint16_t cursor = 0;
  uint16_t pixel = 0;
  while (cursor + 1 < frame.length &&
         pixel < pet_assets::kFrameWidth * pet_assets::kFrameHeight) {
    const uint8_t count = frame.data[cursor++];
    const uint8_t paletteIndex = frame.data[cursor++];
    const int row = pixel / pet_assets::kFrameWidth;
    const int column = pixel % pet_assets::kFrameWidth;
    // The packer deliberately terminates every run at a row boundary.
    if (paletteIndex != 0) {
      canvas.drawFastHLine(x + column, y + row, count,
                           pet_assets::kPalette[paletteIndex]);
    }
    pixel += count;
  }
}

}  // namespace cardbridge
