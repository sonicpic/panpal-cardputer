#pragma once

#include <M5Cardputer.h>

#include "audio_tx.h"
#include "models.h"
#include "pairing.h"
#include "settings_store.h"
#include "wifi_mgr.h"

namespace cardbridge {

class DeviceUi {
 public:
  DeviceUi(SettingsStore& store, WifiManager& wifi, PairingManager& pairing,
           AudioTransmitter& audio, DeviceSettings& settings)
      : store_(store),
        wifi_(wifi),
        pairing_(pairing),
        audio_(audio),
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

  bool keyEvent() const;
  bool pressed(char character) const;
  bool navUp() const;
  bool navDown() const;
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
  void noteActivity();
  void updateScreenPower();

  void draw();
  void drawStatusBar();
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
  String clipped(const String& value, size_t length) const;

  SettingsStore& store_;
  WifiManager& wifi_;
  PairingManager& pairing_;
  AudioTransmitter& audio_;
  DeviceSettings& settings_;

  Page page_ = Page::Main;
  uint8_t mainSelection_ = 0;
  uint8_t settingsSelection_ = 0;
  size_t listSelection_ = 0;
  uint8_t comingAssistant_ = 0;
  String pendingSsid_;
  String textEntry_;
  bool dirty_ = true;
  bool screenOff_ = false;
  bool consumesKeyboard_ = false;
  bool suppressUntilRelease_ = false;
  uint32_t lastActivityMs_ = 0;
  uint32_t lastStatusDrawMs_ = 0;
  uint32_t lastComputerScanMs_ = 0;
  uint32_t lastPageRefreshMs_ = 0;
};

}  // namespace cardbridge
