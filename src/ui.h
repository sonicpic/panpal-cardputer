#pragma once

#include <M5Cardputer.h>

#include "audio_tx.h"
#include "key_tx.h"
#include "models.h"
#include "pairing.h"
#include "settings_store.h"
#include "wifi_mgr.h"

namespace cardbridge {

// v4 UX: one keyboard, two masters, made explicit.
//  - Remote mode: every key goes to the Mac; screen shows a status panel.
//  - Local mode: every key drives this UI with natural bindings
//    (;.,/ + ijkl arrows, Enter, `/Backspace back) — no Fn chords.
//  - BtnA (the physical button beside the screen) toggles modes; it is not
//    part of the keyboard so it can never collide with typing.
// All rendering goes through an off-screen canvas to kill flicker.
enum class UiMode : uint8_t { Local, Remote };

class DeviceUi {
 public:
  DeviceUi(SettingsStore& store, WifiManager& wifi, PairingManager& pairing,
           AudioTransmitter& audio, KeyTransmitter& keys,
           DeviceSettings& settings)
      : store_(store),
        wifi_(wifi),
        pairing_(pairing),
        audio_(audio),
        keys_(keys),
        settings_(settings) {}

  void begin();
  void tick();
  bool consumesKeyboard() const { return consumesKeyboard_; }

 private:
  enum class Page : uint8_t {
    Main,
    ComingSoon,
    Settings,
    Wifi,
    WifiPassword,
    Computers,
    AddComputer,
    PairCode,
  };

  // input
  bool pressed(char character) const;
  bool navUp() const;
  bool navDown() const;
  bool navLeft() const;
  bool navRight() const;
  bool enterPressed() const;
  bool backPressed() const;
  void handleInput();
  void handleMain();
  void handleSettings();
  void handleWifi();
  void handlePassword();
  void handleComputers();
  void handleAddComputer();
  void handlePairCode();
  void appendTypedText(String& destination, size_t maxLength, bool digitsOnly);
  void setPage(Page page);
  void setMode(UiMode mode);
  void noteActivity();
  void updateScreenPower();

  // drawing (all onto canvas_)
  void render();
  void drawStatusBar();
  void drawRemotePanel();
  void drawMain();
  void drawComingSoon();
  void drawSettings();
  void drawWifi();
  void drawPassword();
  void drawComputers();
  void drawAddComputer();
  void drawPairCode();
  void drawMenuRow(int y, bool selected, const String& text,
                   const String& value = String());
  void drawHint(const String& text);
  void drawWifiBars(int x, int y, int rssi, bool connected);
  void drawBattery(int x, int y);
  String clipped(const String& value, size_t length) const;

  SettingsStore& store_;
  WifiManager& wifi_;
  PairingManager& pairing_;
  AudioTransmitter& audio_;
  KeyTransmitter& keys_;
  DeviceSettings& settings_;

  M5Canvas canvas_{&M5Cardputer.Display};
  UiMode mode_ = UiMode::Local;
  Page page_ = Page::Main;
  uint8_t mainSelection_ = 0;
  uint8_t settingsSelection_ = 0;
  size_t listSelection_ = 0;
  uint8_t comingAssistant_ = 0;
  String pendingSsid_;
  String textEntry_;
  bool screenOff_ = false;
  bool consumesKeyboard_ = false;
  bool suppressUntilRelease_ = false;
  bool wasConnected_ = false;
  uint32_t lastActivityMs_ = 0;
  uint32_t lastRenderMs_ = 0;
  uint32_t lastComputerScanMs_ = 0;
};

}  // namespace cardbridge
