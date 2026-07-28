#include "adpcm.h"

#include <cstring>

namespace cardbridge {
namespace {

constexpr int kIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

constexpr int kStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767,
};

uint8_t encodeNibble(int sample, int& predictor, int& index) {
  const int step = kStepTable[index];
  int difference = sample - predictor;
  uint8_t code = 0;
  if (difference < 0) {
    code = 8;
    difference = -difference;
  }
  int delta = step >> 3;
  if (difference >= step) {
    code |= 4;
    difference -= step;
    delta += step;
  }
  if (difference >= (step >> 1)) {
    code |= 2;
    difference -= step >> 1;
    delta += step >> 1;
  }
  if (difference >= (step >> 2)) {
    code |= 1;
    delta += step >> 2;
  }
  predictor += (code & 8) ? -delta : delta;
  predictor = constrain(predictor, -32768, 32767);
  index = constrain(index + kIndexTable[code], 0, 88);
  return code;
}

}  // namespace

bool encodeImaAdpcm(const int16_t* samples, size_t sampleCount,
                    uint8_t* output, size_t outputCapacity,
                    ImaAdpcmState& state) {
  if (!samples || !output || sampleCount != 320 || outputCapacity < 160) {
    return false;
  }
  memset(output, 0, 160);
  int predictor = samples[0];
  int index = 0;
  state.predictor = static_cast<int16_t>(predictor);
  state.index = static_cast<uint8_t>(index);
  for (size_t sample = 1; sample < sampleCount; ++sample) {
    const uint8_t nibble = encodeNibble(samples[sample], predictor, index);
    const size_t nibbleIndex = sample - 1;
    if ((nibbleIndex & 1) == 0) {
      output[nibbleIndex / 2] = nibble;
    } else {
      output[nibbleIndex / 2] |= static_cast<uint8_t>(nibble << 4);
    }
  }
  return true;
}

}  // namespace cardbridge
