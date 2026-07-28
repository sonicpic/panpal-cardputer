#pragma once

#include <Arduino.h>

namespace cardbridge {

struct ImaAdpcmState {
  int16_t predictor = 0;
  uint8_t index = 0;
};

// Encodes 320 PCM16 samples into 160 low-nibble-first IMA ADPCM bytes. The
// first PCM sample is carried as the predictor; 319 nibbles follow.
bool encodeImaAdpcm(const int16_t* samples, size_t sampleCount,
                    uint8_t* output, size_t outputCapacity,
                    ImaAdpcmState& state);

}  // namespace cardbridge
