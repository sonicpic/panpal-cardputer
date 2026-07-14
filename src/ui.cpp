#include "ui.h"

namespace cardbridge {
namespace {

constexpr int kStatusHeight = 18;
constexpr uint16_t kBackground = 0x0841;
constexpr uint16_t kPanel = 0x1082;
constexpr uint16_t kAccent = 0x05FF;
constexpr uint16_t kSelected = 0x032D;

}  // namespace

void DeviceUi::begin() {
  auto& display = M5Cardputer.Display;
  display.setRotation(1);
  display.setTextFont(1);
  display.setTextSize(1);
  display.setBrightness(settings_.brightness);
  lastActivityMs_ = millis();
  draw();
}

void DeviceUi::tick() {
  if (suppressUntilRelease_ && !M5Cardputer.Keyboard.isPressed()) {
    suppressUntilRelease_ = false;
  }
  consumesKeyboard_ = page_ != Page::Main;
  if (M5Cardputer.Keyboard.isPressed() || M5Cardputer.BtnA.isPressed()) {
    if (screenOff_) {
      screenOff_ = false;
      M5Cardputer.Display.setBrightness(settings_.brightness);
      dirty_ = true;
    }
    noteActivity();
  }

  updateScreenPower();
  if (screenOff_) {
    consumesKeyboard_ = true;
    return;
  }

  if (wifi_.needsSetup() && page_ == Page::Main) {
    wifi_.acknowledgeSetup();
    wifi_.startScan();
    setPage(Page::Wifi);
  }
  if (pairing_.pairCodeRequested() && page_ != Page::PairCode) {
    textEntry_.clear();
    setPage(Page::PairCode);
  } else if (page_ == Page::PairCode && pairing_.connected()) {
    setPage(Page::Computers);
  }

  if ((page_ == Page::Computers || page_ == Page::AddComputer) &&
      millis() - lastComputerScanMs_ >= 10000) {
    pairing_.requestDiscovery();
    lastComputerScanMs_ = millis();
  }
  if ((page_ == Page::Computers || page_ == Page::AddComputer ||
       page_ == Page::PairCode || page_ == Page::Wifi) &&
      millis() - lastPageRefreshMs_ >= 1000) {
    // Async WiFi scans and mDNS discovery finish with no key event, so list
    // pages must repaint on a timer to show fresh results.
    dirty_ = true;
    lastPageRefreshMs_ = millis();
  }

  if (keyEvent()) {
    const auto& state = M5Cardputer.Keyboard.keysState();
    if (page_ != Page::Main ||
        (state.fn && (pressed('i') || pressed('k') || state.enter || state.del))) {
      suppressUntilRelease_ = true;
    }
    handleInput();
  }
  if (dirty_) {
    draw();
  } else if (millis() - lastStatusDrawMs_ >= 500) {
    drawStatusBar();
    if (page_ == Page::Main) {
      const int width = map(audio_.level(), 0, 255, 0, 226);
      M5Cardputer.Display.fillRect(7, 119, 226, 8, TFT_DARKGREY);
      M5Cardputer.Display.fillRect(7, 119, width, 8,
                                   audio_.muted() ? TFT_RED : TFT_GREEN);
    }
  }

  // On the main page only Fn navigation is consumed. Ordinary typing remains
  // an always-on service and is forwarded regardless of the selected menu row.
  if (page_ == Page::Main) {
    const auto& state = M5Cardputer.Keyboard.keysState();
    consumesKeyboard_ = suppressUntilRelease_ || (state.fn &&
        (pressed('i') || pressed('k') || state.enter || state.del));
  }
}

bool DeviceUi::keyEvent() const {
  return M5Cardputer.Keyboard.isChange();
}

bool DeviceUi::pressed(char character) const {
  const auto& state = M5Cardputer.Keyboard.keysState();
  for (char c : state.word) {
    if (tolower(static_cast<unsigned char>(c)) ==
        tolower(static_cast<unsigned char>(character))) return true;
  }
  return false;
}

bool DeviceUi::navUp() const {
  const auto& state = M5Cardputer.Keyboard.keysState();
  return pressed('i') && (page_ != Page::Main || state.fn);
}

bool DeviceUi::navDown() const {
  const auto& state = M5Cardputer.Keyboard.keysState();
  return pressed('k') && (page_ != Page::Main || state.fn);
}

bool DeviceUi::enterPressed() const {
  const auto& state = M5Cardputer.Keyboard.keysState();
  return state.enter && (page_ != Page::Main || state.fn);
}

bool DeviceUi::backPressed() const {
  return M5Cardputer.Keyboard.keysState().del;
}

void DeviceUi::handleInput() {
  if (!M5Cardputer.Keyboard.isPressed()) return;
  noteActivity();
  switch (page_) {
    case Page::Main: handleMain(); break;
    case Page::ComingSoon: if (backPressed()) setPage(Page::Main); break;
    case Page::Settings: handleSettings(); break;
    case Page::Wifi: handleWifi(); break;
    case Page::WifiPassword: handlePassword(); break;
    case Page::Computers: handleComputers(); break;
    case Page::AddComputer: handleAddComputer(); break;
    case Page::PairCode: handlePairCode(); break;
  }
}

void DeviceUi::handleMain() {
  if (navUp()) {
    mainSelection_ = (mainSelection_ + 2) % 3;
    dirty_ = true;
  } else if (navDown()) {
    mainSelection_ = (mainSelection_ + 1) % 3;
    dirty_ = true;
  } else if (enterPressed()) {
    if (mainSelection_ < 2) {
      comingAssistant_ = mainSelection_;
      setPage(Page::ComingSoon);
    } else {
      setPage(Page::Settings);
    }
  }
}

void DeviceUi::handleSettings() {
  constexpr uint8_t itemCount = 7;
  if (navUp()) {
    settingsSelection_ = (settingsSelection_ + itemCount - 1) % itemCount;
    dirty_ = true;
  } else if (navDown()) {
    settingsSelection_ = (settingsSelection_ + 1) % itemCount;
    dirty_ = true;
  } else if (backPressed()) {
    setPage(Page::Main);
  } else if (enterPressed()) {
    switch (settingsSelection_) {
      case 0:
        listSelection_ = 0;
        wifi_.startScan();
        setPage(Page::Wifi);
        break;
      case 1:
        listSelection_ = 0;
        pairing_.requestDiscovery();
        setPage(Page::Computers);
        break;
      case 2:
        settings_.micMuted = !settings_.micMuted;
        audio_.setMuted(settings_.micMuted);
        store_.saveSettings(settings_);
        dirty_ = true;
        break;
      case 3: {
        settings_.typelessFunctionKey =
            settings_.typelessFunctionKey >= 16
                ? 13 : settings_.typelessFunctionKey + 1;
        store_.saveSettings(settings_);
        dirty_ = true;
        break;
      }
      case 4: {
        const uint8_t levels[] = {64, 128, 192, 255};
        size_t index = 0;
        while (index < 3 && settings_.brightness > levels[index]) ++index;
        settings_.brightness = levels[(index + 1) % 4];
        M5Cardputer.Display.setBrightness(settings_.brightness);
        store_.saveSettings(settings_);
        dirty_ = true;
        break;
      }
      case 5: {
        const uint16_t times[] = {30, 60, 120, 300, 0};
        size_t index = 0;
        while (index < 4 && settings_.screenTimeoutSec != times[index]) ++index;
        settings_.screenTimeoutSec = times[(index + 1) % 5];
        store_.saveSettings(settings_);
        dirty_ = true;
        break;
      }
      case 6: setPage(Page::Main); break;
    }
  }
}

void DeviceUi::handleWifi() {
  const size_t count = wifi_.scanCount();
  if (pressed('r') || M5Cardputer.Keyboard.keysState().tab) {
    wifi_.startScan();
    dirty_ = true;
  } else if (backPressed() && M5Cardputer.Keyboard.keysState().fn && count > 0) {
    wifi_.forget(wifi_.scanResult(listSelection_).ssid);
    dirty_ = true;
  } else if (backPressed()) {
    setPage(Page::Settings);
  } else if (count > 0 && navUp()) {
    listSelection_ = (listSelection_ + count - 1) % count;
    dirty_ = true;
  } else if (count > 0 && navDown()) {
    listSelection_ = (listSelection_ + 1) % count;
    dirty_ = true;
  } else if (count > 0 && enterPressed()) {
    const WifiScanResult& selected = wifi_.scanResult(listSelection_);
    if (selected.saved) {
      for (size_t i = 0; i < wifi_.savedCount(); ++i) {
        if (wifi_.saved(i).ssid == selected.ssid) {
          wifi_.connectSaved(i);
          setPage(Page::Settings);
          break;
        }
      }
    } else if (selected.encrypted) {
      pendingSsid_ = selected.ssid;
      textEntry_.clear();
      setPage(Page::WifiPassword);
    } else {
      wifi_.addAndConnect(selected.ssid, "");
      setPage(Page::Settings);
    }
  }
}

void DeviceUi::handlePassword() {
  const auto& state = M5Cardputer.Keyboard.keysState();
  if (state.fn && state.del) {
    setPage(Page::Wifi);
  } else if (state.enter) {
    wifi_.addAndConnect(pendingSsid_, textEntry_);
    textEntry_.clear();
    setPage(Page::Settings);
  } else if (state.del) {
    if (!textEntry_.isEmpty()) textEntry_.remove(textEntry_.length() - 1);
    dirty_ = true;
  } else {
    appendTypedText(textEntry_, 63, false);
  }
}

void DeviceUi::handleComputers() {
  const size_t rows = pairing_.pairedCount() + 2;  // Add new + Back.
  if (backPressed() && M5Cardputer.Keyboard.keysState().fn &&
      listSelection_ < pairing_.pairedCount()) {
    pairing_.deletePairing(listSelection_);
    if (listSelection_ >= pairing_.pairedCount() && listSelection_ > 0) --listSelection_;
    dirty_ = true;
  } else if (backPressed()) {
    setPage(Page::Settings);
  } else if (navUp()) {
    listSelection_ = (listSelection_ + rows - 1) % rows;
    dirty_ = true;
  } else if (navDown()) {
    listSelection_ = (listSelection_ + 1) % rows;
    dirty_ = true;
  } else if (enterPressed()) {
    if (listSelection_ < pairing_.pairedCount()) {
      if (pairing_.pairedCurrent(listSelection_)) {
        pairing_.disconnect(true);
      } else {
        pairing_.connectToPaired(listSelection_);
      }
      dirty_ = true;
    } else if (listSelection_ == pairing_.pairedCount()) {
      listSelection_ = 0;
      pairing_.requestDiscovery();
      setPage(Page::AddComputer);
    } else {
      setPage(Page::Settings);
    }
  }
}

void DeviceUi::handleAddComputer() {
  const size_t count = pairing_.discoveredCount();
  if (pressed('r') || M5Cardputer.Keyboard.keysState().tab) {
    pairing_.requestDiscovery();
    dirty_ = true;
  } else if (backPressed()) {
    setPage(Page::Computers);
  } else if (count > 0 && navUp()) {
    listSelection_ = (listSelection_ + count - 1) % count;
    dirty_ = true;
  } else if (count > 0 && navDown()) {
    listSelection_ = (listSelection_ + 1) % count;
    dirty_ = true;
  } else if (count > 0 && enterPressed()) {
    pairing_.connectToDiscovered(listSelection_);
    dirty_ = true;
  }
}

void DeviceUi::handlePairCode() {
  const auto& state = M5Cardputer.Keyboard.keysState();
  if (state.fn && state.del) {
    pairing_.disconnect(true);
    setPage(Page::Computers);
  } else if (state.enter && textEntry_.length() == 6) {
    pairing_.submitPairCode(textEntry_);
    dirty_ = true;
  } else if (state.del) {
    if (!textEntry_.isEmpty()) textEntry_.remove(textEntry_.length() - 1);
    dirty_ = true;
  } else {
    appendTypedText(textEntry_, 6, true);
  }
}

void DeviceUi::appendTypedText(String& destination, size_t maxLength,
                               bool digitsOnly) {
  const auto& state = M5Cardputer.Keyboard.keysState();
  for (char c : state.word) {
    if (destination.length() >= maxLength) break;
    if (digitsOnly && !isDigit(c)) continue;
    if (c >= 32 && c <= 126) destination += c;
  }
  dirty_ = true;
}

void DeviceUi::setPage(Page page) {
  page_ = page;
  listSelection_ = 0;
  dirty_ = true;
}

void DeviceUi::noteActivity() {
  lastActivityMs_ = millis();
}

void DeviceUi::updateScreenPower() {
  if (screenOff_ || settings_.screenTimeoutSec == 0) return;
  if (millis() - lastActivityMs_ >= settings_.screenTimeoutSec * 1000UL) {
    M5Cardputer.Display.setBrightness(0);
    screenOff_ = true;
  }
}

void DeviceUi::draw() {
  auto& display = M5Cardputer.Display;
  display.fillScreen(kBackground);
  drawStatusBar();
  switch (page_) {
    case Page::Main: drawMain(); break;
    case Page::ComingSoon: drawComingSoon(); break;
    case Page::Settings: drawSettings(); break;
    case Page::Wifi: drawWifi(); break;
    case Page::WifiPassword: drawPassword(); break;
    case Page::Computers: drawComputers(); break;
    case Page::AddComputer: drawAddComputer(); break;
    case Page::PairCode: drawPairCode(); break;
  }
  dirty_ = false;
}

void DeviceUi::drawStatusBar() {
  auto& display = M5Cardputer.Display;
  display.fillRect(0, 0, display.width(), kStatusHeight, TFT_BLACK);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setCursor(3, 5);
  if (wifi_.connected()) {
    display.printf("%s %dd", clipped(wifi_.currentSsid(), 9).c_str(), wifi_.rssi());
  } else {
    display.print("WiFi --");
  }
  display.setCursor(91, 5);
  display.print(clipped(pairing_.connected() ? pairing_.connectedName() : "Mac --", 12));
  display.setCursor(177, 5);
  display.print(audio_.muted() ? "MUTE" : (pairing_.connected() ? "MIC" : "MIC-"));
  int battery = M5Cardputer.Power.getBatteryLevel();
  display.setCursor(211, 5);
  if (battery >= 0) display.printf("%d%%", battery);
  lastStatusDrawMs_ = millis();
}

void DeviceUi::drawMain() {
  drawMenuRow(25, mainSelection_ == 0, "Claude assistant", ">");
  drawMenuRow(51, mainSelection_ == 1, "Codex assistant", ">");
  drawMenuRow(77, mainSelection_ == 2, "Settings", ">");
  auto& display = M5Cardputer.Display;
  display.setTextColor(TFT_LIGHTGREY, kBackground);
  display.setCursor(7, 105);
  display.print("Fn+I/K select  Fn+Enter open");
  display.fillRect(7, 119, 226, 8, TFT_DARKGREY);
}

void DeviceUi::drawComingSoon() {
  auto& display = M5Cardputer.Display;
  display.setTextDatum(middle_center);
  display.setTextColor(kAccent, kBackground);
  display.setTextSize(2);
  display.drawString(comingAssistant_ == 0 ? "Claude" : "Codex", 120, 49);
  display.setTextSize(1);
  display.setTextColor(TFT_WHITE, kBackground);
  display.drawString("Coming soon", 120, 78);
  display.drawString("Backspace: back", 120, 108);
  display.setTextDatum(top_left);
}

void DeviceUi::drawSettings() {
  const char* mic = settings_.micMuted ? "Muted" : "Live";
  String timeout = settings_.screenTimeoutSec == 0
      ? String("Never") : String(settings_.screenTimeoutSec) + "s";
  drawMenuRow(20, settingsSelection_ == 0, "WiFi", clipped(wifi_.currentSsid(), 10));
  drawMenuRow(36, settingsSelection_ == 1, "Computers",
              String(pairing_.pairedCount()));
  drawMenuRow(52, settingsSelection_ == 2, "Microphone", mic);
  drawMenuRow(68, settingsSelection_ == 3, "Typeless key",
              String("F") + String(settings_.typelessFunctionKey));
  drawMenuRow(84, settingsSelection_ == 4, "Brightness",
              String(settings_.brightness));
  drawMenuRow(100, settingsSelection_ == 5, "Screen off", timeout);
  drawMenuRow(116, settingsSelection_ == 6, "Back");
}

void DeviceUi::drawWifi() {
  auto& display = M5Cardputer.Display;
  display.setTextColor(TFT_WHITE, kBackground);
  display.setCursor(5, 21);
  display.print(wifi_.scanning() ? "Scanning WiFi..." : "WiFi networks  R:rescan");
  const size_t count = wifi_.scanCount();
  const size_t first = listSelection_ >= 5 ? listSelection_ - 4 : 0;
  for (size_t row = 0; row < 5 && first + row < count; ++row) {
    const auto& item = wifi_.scanResult(first + row);
    String value = wifi_.connected() && wifi_.currentSsid() == item.ssid
                       ? String("CUR")
                       : (item.rssi <= -127 ? String("--") : String(item.rssi));
    value += (item.encrypted ? " L" : "");
    value += (item.saved ? " S" : "");
    drawMenuRow(38 + row * 18, first + row == listSelection_,
                clipped(item.ssid, 17), value);
  }
  if (!wifi_.scanning() && count == 0) {
    display.setCursor(5, 58);
    display.print("No 2.4GHz networks found");
  }
}

void DeviceUi::drawPassword() {
  auto& display = M5Cardputer.Display;
  display.setTextColor(TFT_WHITE, kBackground);
  display.setCursor(7, 29);
  display.print("Password for:");
  display.setTextColor(kAccent, kBackground);
  display.setCursor(7, 44);
  display.print(clipped(pendingSsid_, 30));
  display.drawRect(6, 65, 228, 25, TFT_DARKGREY);
  display.setCursor(11, 73);
  for (size_t i = 0; i < textEntry_.length(); ++i) display.print('*');
  display.setTextColor(TFT_LIGHTGREY, kBackground);
  display.setCursor(7, 106);
  display.print("Enter: connect  Fn+Back: cancel");
}

void DeviceUi::drawComputers() {
  auto& display = M5Cardputer.Display;
  display.setTextColor(TFT_WHITE, kBackground);
  display.setCursor(5, 21);
  display.print("Paired Macs  Fn+Back: delete");
  const size_t total = pairing_.pairedCount() + 2;
  const size_t first = listSelection_ >= 5 ? listSelection_ - 4 : 0;
  for (size_t row = 0; row < 5 && first + row < total; ++row) {
    const size_t index = first + row;
    if (index < pairing_.pairedCount()) {
      String state = pairing_.pairedCurrent(index) ? "CURRENT" :
                     (pairing_.pairedOnline(index) ? "online" : "offline");
      drawMenuRow(38 + row * 18, index == listSelection_,
                  clipped(pairing_.paired(index).name, 16), state);
    } else if (index == pairing_.pairedCount()) {
      drawMenuRow(38 + row * 18, index == listSelection_, "+ Add new computer");
    } else {
      drawMenuRow(38 + row * 18, index == listSelection_, "Back");
    }
  }
}

void DeviceUi::drawAddComputer() {
  auto& display = M5Cardputer.Display;
  display.setTextColor(TFT_WHITE, kBackground);
  display.setCursor(5, 21);
  display.print("Nearby Macs  R:rescan");
  const size_t count = pairing_.discoveredCount();
  const size_t first = listSelection_ >= 5 ? listSelection_ - 4 : 0;
  for (size_t row = 0; row < 5 && first + row < count; ++row) {
    const auto& item = pairing_.discovered(first + row);
    drawMenuRow(38 + row * 18, first + row == listSelection_,
                clipped(item.name, 19), item.paired ? "paired" : "new");
  }
  if (count == 0) {
    display.setCursor(5, 58);
    display.print("Searching for CardBridge...");
  }
}

void DeviceUi::drawPairCode() {
  auto& display = M5Cardputer.Display;
  display.setTextColor(TFT_WHITE, kBackground);
  display.setCursor(7, 27);
  display.print("Enter 6-digit code shown on Mac");
  display.setTextSize(3);
  display.setTextColor(kAccent, kBackground);
  display.setTextDatum(middle_center);
  String code = textEntry_;
  while (code.length() < 6) code += "-";
  display.drawString(code, 120, 69);
  display.setTextDatum(top_left);
  display.setTextSize(1);
  display.setTextColor(TFT_LIGHTGREY, kBackground);
  display.setCursor(7, 108);
  display.print("Enter: pair  Fn+Back: cancel");
}

void DeviceUi::drawMenuRow(int y, bool selected, const String& text,
                           const String& value) {
  auto& display = M5Cardputer.Display;
  const uint16_t background = selected ? kSelected : kPanel;
  display.fillRoundRect(4, y, 232, 16, 3, background);
  display.setTextColor(selected ? TFT_WHITE : TFT_LIGHTGREY, background);
  display.setCursor(8, y + 4);
  display.print(text);
  if (!value.isEmpty()) {
    const int width = display.textWidth(value);
    display.setCursor(231 - width, y + 4);
    display.print(value);
  }
}

String DeviceUi::clipped(const String& value, size_t length) const {
  if (value.length() <= length) return value;
  if (length < 2) return value.substring(0, length);
  return value.substring(0, length - 1) + "~";
}

}  // namespace cardbridge
