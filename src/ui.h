#pragma once

#include <M5Cardputer.h>

#include "audio_tx.h"
#include "key_tx.h"
#include "models.h"
#include "pairing.h"
#include "pet_renderer.h"
#include "settings_store.h"
#include "wifi_mgr.h"

namespace cardbridge {

// v4 UX: one keyboard, two masters, made explicit.
//  - Remote mode: every key goes to the Mac while the current page remains
//    visible; a keyboard icon in the status bar shows that forwarding is on.
//  - Local mode: every key drives this UI with natural bindings
//    (;.,/ + ijkl arrows, Enter, Esc/Backspace) — no Fn chords.
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
  // Exposed so the serial console can drive/inspect the mode during bring-up.
  void toggleMode() { setMode(mode_ == UiMode::Remote ? UiMode::Local : UiMode::Remote); }
  void showCodex();
  const char* modeName() const { return mode_ == UiMode::Remote ? "remote" : "local"; }

 private:
  enum class Page : uint8_t {
    Main,
    ComingSoon,
    Codex,
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
  bool escapePressed() const;
  bool backspacePressed() const;
  bool backPressed() const;
  void handleInput();
  void handleMain();
  void handleCodex();
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
  String statusBarTitle() const;
  void drawScrollingTitle(const String& title);
  void drawScrollingActivity(const String& activity);
  void drawKeyboardModeIcon(int x, int y);
  void drawMain();
  void drawComingSoon();
  void drawCodex();
  void drawQuotaHud(int x, int remaining, const char* label,
                    uint16_t color);
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
  void drawWifiStrengthIcon(int x, int y, int rssi, uint16_t active,
                            uint16_t inactive);
  void drawBattery(int x, int y);
  String clipped(const String& value, size_t length) const;
  const AgentSession* selectedAgent() const;

  SettingsStore& store_;
  WifiManager& wifi_;
  PairingManager& pairing_;
  AudioTransmitter& audio_;
  KeyTransmitter& keys_;
  DeviceSettings& settings_;

  M5Canvas canvas_{&M5Cardputer.Display};
  PetRenderer pet_;
  UiMode mode_ = UiMode::Local;
  Page page_ = Page::Main;
  uint8_t mainSelection_ = 0;
  uint8_t settingsSelection_ = 0;
  size_t listSelection_ = 0;
  uint8_t comingAssistant_ = 0;
  size_t agentSelection_ = 0;
  String selectedAgentId_;
  String lastAgentFocusId_;
  uint32_t lastAgentFocusSeq_ = 0;
  String marqueeTitle_;
  uint32_t marqueeStartedMs_ = 0;
  String marqueeActivity_;
  uint32_t marqueeActivityStartedMs_ = 0;
  String pendingSsid_;
  String textEntry_;
  bool screenOff_ = false;
  bool consumesKeyboard_ = false;
  bool suppressUntilRelease_ = false;
  uint32_t lastActivityMs_ = 0;
  uint32_t lastRenderMs_ = 0;
  uint32_t lastComputerScanMs_ = 0;
};

}  // namespace cardbridge
