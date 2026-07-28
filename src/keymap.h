#pragma once

#include <Arduino.h>

namespace cardbridge {

// This file is the single place for Cardputer keyboard mappings. Push-to-talk
// is handled separately by VoiceController, so Fn+Space has no key mapping.
const char* mapFnKey(uint8_t physicalKey, uint8_t typelessFunctionKey = 13);
const char* mapSpecialKey(uint8_t physicalKey);
bool isKeyboardModifier(uint8_t physicalKey);
const char* mapModifier(uint8_t physicalKey);

}  // namespace cardbridge
