#include "keymap.h"

#include <M5Cardputer.h>

namespace cardbridge {

const char* mapFnKey(uint8_t key, uint8_t typelessFunctionKey) {
  (void)typelessFunctionKey;  // Kept in the signature for stored-setting compatibility.
  switch (key) {
    // Fn+Space is consumed by VoiceController and sent as semantic voice
    // down/up edges. It intentionally does not masquerade as F13.
    case ' ': return nullptr;
    // Follow the arrow legends printed on the Cardputer keyboard.
    case ';': return "up";
    case ',': return "left";
    case '.': return "down";
    case '/': return "right";
    case '`': return "escape";
    case '[': return "home";
    case ']': return "end";
    case KEY_BACKSPACE: return "delete_forward";
    default: return nullptr;
  }
}

const char* mapSpecialKey(uint8_t key) {
  switch (key) {
    case KEY_BACKSPACE: return "backspace";
    case KEY_TAB: return "tab";
    case KEY_ENTER: return "enter";
    default: return nullptr;
  }
}

bool isKeyboardModifier(uint8_t key) {
  return key == KEY_LEFT_CTRL || key == KEY_LEFT_SHIFT || key == KEY_LEFT_ALT ||
         key == KEY_OPT;
}

const char* mapModifier(uint8_t key) {
  switch (key) {
    case KEY_LEFT_CTRL: return "ctrl";
    case KEY_LEFT_ALT: return "cmd";
    case KEY_OPT: return "alt";
    default: return nullptr;
  }
}

}  // namespace cardbridge
