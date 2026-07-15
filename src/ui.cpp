#include "ui.h"

namespace cardbridge {
namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 135;
constexpr int kStatusHeight = 20;
constexpr uint16_t kBackground = 0x0841;   // near-black blue
constexpr uint16_t kPanel = 0x18E3;        // card body
constexpr uint16_t kPanelSelected = 0x0339;
constexpr uint16_t kAccent = 0x05FF;       // cyan
constexpr uint16_t kAccentWarm = 0xFD20;   // orange
constexpr uint16_t kTextDim = 0x8410;
constexpr uint16_t kGood = 0x07E9;
constexpr uint16_t kBad = 0xF9E7;

}  // namespace

void DeviceUi::begin() {
  auto& display = M5Cardputer.Display;
  display.setRotation(1);
  display.setBrightness(settings_.brightness);
  canvas_.setColorDepth(16);
  void* buffer = canvas_.createSprite(kWidth, kHeight);
  if (!buffer) {
    // Not enough contiguous heap for 16-bit: fall back to 8-bit (half size).
    canvas_.setColorDepth(8);
    buffer = canvas_.createSprite(kWidth, kHeight);
  }
  Serial.printf("[ui] canvas=%p depth=%d heap=%u\n", buffer,
                canvas_.getColorDepth(), ESP.getFreeHeap());
  canvas_.setTextFont(1);
  lastActivityMs_ = millis();
  render();
}

void DeviceUi::setMode(UiMode mode) {
  if (mode_ == mode) return;
  mode_ = mode;
  if (mode_ == UiMode::Local) page_ = Page::Main;
  suppressUntilRelease_ = M5Cardputer.Keyboard.isPressed();
}

void DeviceUi::tick() {
  // The mode is the user's decision alone: connecting/disconnecting never
  // changes it. Auto-entering Remote on connect stole the device out of the
  // user's hands; a live link only means keys *can* be sent, not that they
  // should be. Boot lands in Local; only BtnA leaves it.

  // BtnA: the dedicated physical mode switch (wakes the screen first).
  if (M5Cardputer.BtnA.wasClicked()) {
    Serial.println("[ui] BtnA click");
    noteActivity();
    if (screenOff_) {
      screenOff_ = false;
      M5Cardputer.Display.setBrightness(settings_.brightness);
    } else {
      toggleMode();
    }
  }

  if (suppressUntilRelease_ && !M5Cardputer.Keyboard.isPressed()) {
    suppressUntilRelease_ = false;
  }

  if (M5Cardputer.Keyboard.isPressed()) {
    noteActivity();
    if (screenOff_) {
      screenOff_ = false;
      M5Cardputer.Display.setBrightness(settings_.brightness);
      // In Local mode the waking keypress must not also act on the UI.
      // In Remote mode keys keep flowing to the Mac uninterrupted.
      if (mode_ == UiMode::Local) suppressUntilRelease_ = true;
    }
  }
  updateScreenPower();

  // First-boot funnel: no WiFi credentials -> jump into setup.
  if (wifi_.needsSetup() && mode_ == UiMode::Local && page_ == Page::Main) {
    wifi_.acknowledgeSetup();
    wifi_.startScan();
    setPage(Page::Wifi);
  }
  // Pairing flow interrupts (they require typing, so force Local).
  if (pairing_.pairCodeRequested() && page_ != Page::PairCode) {
    setMode(UiMode::Local);
    textEntry_.clear();
    setPage(Page::PairCode);
  } else if (page_ == Page::PairCode && pairing_.connected()) {
    setPage(Page::Computers);
  }

  if (mode_ == UiMode::Local &&
      (page_ == Page::Computers || page_ == Page::AddComputer) &&
      millis() - lastComputerScanMs_ >= 10000) {
    pairing_.requestDiscovery();
    lastComputerScanMs_ = millis();
  }

  // Input routing.
  consumesKeyboard_ = mode_ == UiMode::Local || suppressUntilRelease_;
  if (mode_ == UiMode::Local && !suppressUntilRelease_ &&
      M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    noteActivity();
    handleInput();
  }

  // Steady 10 fps into the off-screen canvas: no flicker, always fresh.
  if (!screenOff_ && millis() - lastRenderMs_ >= 100) {
    render();
    lastRenderMs_ = millis();
  }
}

// ---------------------------------------------------------------- input --

bool DeviceUi::pressed(char character) const {
  const auto& state = M5Cardputer.Keyboard.keysState();
  for (char c : state.word) {
    if (tolower(static_cast<unsigned char>(c)) ==
        tolower(static_cast<unsigned char>(character))) return true;
  }
  return false;
}

// Arrow legends on the Cardputer keycaps: ; up  . down  , left  / right.
// ijkl kept as an alternative for one-handed use.
bool DeviceUi::navUp() const { return pressed(';') || pressed('i'); }
bool DeviceUi::navDown() const { return pressed('.') || pressed('k'); }
bool DeviceUi::navLeft() const { return pressed(',') || pressed('j'); }
bool DeviceUi::navRight() const { return pressed('/') || pressed('l'); }
bool DeviceUi::enterPressed() const {
  return M5Cardputer.Keyboard.keysState().enter;
}
bool DeviceUi::backPressed() const {
  return pressed('`') || M5Cardputer.Keyboard.keysState().del;
}

void DeviceUi::handleInput() {
  switch (page_) {
    case Page::Main: handleMain(); break;
    case Page::ComingSoon: if (backPressed() || enterPressed()) setPage(Page::Main); break;
    case Page::Settings: handleSettings(); break;
    case Page::Wifi: handleWifi(); break;
    case Page::WifiPassword: handlePassword(); break;
    case Page::Computers: handleComputers(); break;
    case Page::AddComputer: handleAddComputer(); break;
    case Page::PairCode: handlePairCode(); break;
  }
}

void DeviceUi::handleMain() {
  if (navLeft()) {
    mainSelection_ = (mainSelection_ + 2) % 3;
  } else if (navRight()) {
    mainSelection_ = (mainSelection_ + 1) % 3;
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
  constexpr uint8_t itemCount = 6;
  if (navUp()) {
    settingsSelection_ = (settingsSelection_ + itemCount - 1) % itemCount;
  } else if (navDown()) {
    settingsSelection_ = (settingsSelection_ + 1) % itemCount;
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
        break;
      case 3:
        settings_.typelessFunctionKey =
            settings_.typelessFunctionKey >= 16
                ? 13 : settings_.typelessFunctionKey + 1;
        store_.saveSettings(settings_);
        break;
      case 4: {
        const uint8_t levels[] = {64, 128, 192, 255};
        size_t index = 0;
        while (index < 3 && settings_.brightness > levels[index]) ++index;
        settings_.brightness = levels[(index + 1) % 4];
        M5Cardputer.Display.setBrightness(settings_.brightness);
        store_.saveSettings(settings_);
        break;
      }
      case 5: {
        const uint16_t times[] = {30, 60, 120, 300, 0};
        size_t index = 0;
        while (index < 4 && settings_.screenTimeoutSec != times[index]) ++index;
        settings_.screenTimeoutSec = times[(index + 1) % 5];
        store_.saveSettings(settings_);
        break;
      }
    }
  }
}

void DeviceUi::handleWifi() {
  const size_t count = wifi_.scanCount();
  const auto& state = M5Cardputer.Keyboard.keysState();
  if (state.tab || pressed('r')) {
    wifi_.startScan();
  } else if (state.fn && state.del && count > 0) {
    wifi_.forget(wifi_.scanResult(listSelection_).ssid);
  } else if (backPressed()) {
    setPage(Page::Settings);
  } else if (count > 0 && navUp()) {
    listSelection_ = (listSelection_ + count - 1) % count;
  } else if (count > 0 && navDown()) {
    listSelection_ = (listSelection_ + 1) % count;
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
  } else {
    appendTypedText(textEntry_, 63, false);
  }
}

void DeviceUi::handleComputers() {
  const size_t rows = pairing_.pairedCount() + 2;  // Add new + Back.
  const auto& state = M5Cardputer.Keyboard.keysState();
  if (state.fn && state.del && listSelection_ < pairing_.pairedCount()) {
    pairing_.deletePairing(listSelection_);
    if (listSelection_ >= pairing_.pairedCount() && listSelection_ > 0) --listSelection_;
  } else if (backPressed()) {
    setPage(Page::Settings);
  } else if (navUp()) {
    listSelection_ = (listSelection_ + rows - 1) % rows;
  } else if (navDown()) {
    listSelection_ = (listSelection_ + 1) % rows;
  } else if (enterPressed()) {
    if (listSelection_ < pairing_.pairedCount()) {
      if (pairing_.pairedCurrent(listSelection_)) {
        pairing_.disconnect(true);
      } else {
        pairing_.connectToPaired(listSelection_);
      }
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
  const auto& state = M5Cardputer.Keyboard.keysState();
  if (state.tab || pressed('r')) {
    pairing_.requestDiscovery();
  } else if (backPressed()) {
    setPage(Page::Computers);
  } else if (count > 0 && navUp()) {
    listSelection_ = (listSelection_ + count - 1) % count;
  } else if (count > 0 && navDown()) {
    listSelection_ = (listSelection_ + 1) % count;
  } else if (count > 0 && enterPressed()) {
    pairing_.connectToDiscovered(listSelection_);
  }
}

void DeviceUi::handlePairCode() {
  const auto& state = M5Cardputer.Keyboard.keysState();
  if (state.fn && state.del) {
    pairing_.disconnect(true);
    setPage(Page::Computers);
  } else if (state.enter && textEntry_.length() == 6) {
    pairing_.submitPairCode(textEntry_);
  } else if (state.del) {
    if (!textEntry_.isEmpty()) textEntry_.remove(textEntry_.length() - 1);
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
}

void DeviceUi::setPage(Page page) {
  page_ = page;
  listSelection_ = 0;
  suppressUntilRelease_ = M5Cardputer.Keyboard.isPressed();
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

// -------------------------------------------------------------- drawing --

void DeviceUi::render() {
  if (!canvas_.getBuffer()) return;  // allocation failed — never draw blind
  canvas_.fillSprite(kBackground);
  drawStatusBar();
  if (mode_ == UiMode::Remote) {
    drawRemotePanel();
  } else {
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
  }
  // Explicit destination: the sprite's stored parent pointer is unreliable
  // when the canvas member is constructed before M5Cardputer (static-init
  // order across translation units).
  canvas_.pushSprite(&M5Cardputer.Display, 0, 0);
}

void DeviceUi::drawWifiBars(int x, int y, int rssi, bool connected) {
  const int bars = !connected ? 0 : rssi > -55 ? 4 : rssi > -65 ? 3
                   : rssi > -75 ? 2 : 1;
  for (int i = 0; i < 4; ++i) {
    const int h = 3 + i * 3;
    canvas_.fillRect(x + i * 4, y + 12 - h, 3, h,
                     i < bars ? kGood : kPanel);
  }
}

void DeviceUi::drawBattery(int x, int y) {
  const int level = M5Cardputer.Power.getBatteryLevel();
  canvas_.drawRect(x, y, 20, 10, kTextDim);
  canvas_.fillRect(x + 20, y + 3, 2, 4, kTextDim);
  if (level > 0) {
    const uint16_t color = level > 30 ? kGood : kBad;
    canvas_.fillRect(x + 2, y + 2, max(1, (20 - 4) * level / 100), 6, color);
  }
}

void DeviceUi::drawStatusBar() {
  canvas_.fillRect(0, 0, kWidth, kStatusHeight, TFT_BLACK);
  drawWifiBars(4, 4, wifi_.rssi(), wifi_.connected());
  canvas_.setTextColor(kTextDim, TFT_BLACK);
  canvas_.setTextSize(1);
  canvas_.setCursor(24, 7);
  canvas_.print(clipped(wifi_.connected() ? wifi_.currentSsid() : "--", 8));

  // Link dot + Mac name in the middle.
  const bool linked = pairing_.connected();
  canvas_.fillCircle(88, 10, 3, linked ? kGood : kBad);
  canvas_.setCursor(96, 7);
  canvas_.setTextColor(linked ? TFT_WHITE : kTextDim, TFT_BLACK);
  canvas_.print(clipped(linked ? pairing_.connectedName() : "no Mac", 11));

  // Mic mini level bar.
  const int level = map(audio_.level(), 0, 255, 0, 24);
  canvas_.drawRect(172, 5, 26, 10, kTextDim);
  canvas_.fillRect(173, 6, min(24, level), 8,
                   audio_.muted() ? kBad : kGood);
  drawBattery(210, 5);
}

void DeviceUi::drawHint(const String& text) {
  canvas_.setTextSize(1);
  canvas_.setTextColor(kTextDim, kBackground);
  canvas_.setTextDatum(bottom_center);
  canvas_.drawString(text, kWidth / 2, kHeight - 3);
  canvas_.setTextDatum(top_left);
}

void DeviceUi::drawRemotePanel() {
  // Big, glanceable: you are typing on THAT Mac right now.
  const bool linked = pairing_.connected();
  canvas_.setTextDatum(middle_center);
  canvas_.setTextColor(linked ? kAccent : kBad, kBackground);
  canvas_.setTextSize(2);
  canvas_.drawString("REMOTE", kWidth / 2, 38);
  canvas_.setTextColor(linked ? TFT_WHITE : kTextDim, kBackground);
  canvas_.setTextSize(2);
  canvas_.drawString(linked ? clipped(pairing_.connectedName(), 16)
                            : String("no Mac linked"),
                     kWidth / 2, 62);
  canvas_.setTextSize(1);
  canvas_.setTextColor(kTextDim, kBackground);
  canvas_.drawString(String("keys sent: ") + String(keys_.sentKeys()),
                     kWidth / 2, 84);
  // Live mic meter, wide.
  const int width = map(audio_.level(), 0, 255, 0, 200);
  canvas_.drawRect(19, 96, 202, 10, kTextDim);
  canvas_.fillRect(20, 97, min(200, width), 8,
                   audio_.muted() ? kBad : kGood);
  canvas_.setTextDatum(top_left);
  drawHint("typing goes to the Mac   BtnA: local control");
}

void DeviceUi::drawMain() {
  static const char* names[] = {"Claude", "Codex", "Setup"};
  static const uint16_t colors[] = {kAccentWarm, kAccent, kTextDim};
  const int cardWidth = 68, cardHeight = 74, gap = 8;
  const int totalWidth = 3 * cardWidth + 2 * gap;
  const int x0 = (kWidth - totalWidth) / 2;
  const int y0 = 30;
  for (int i = 0; i < 3; ++i) {
    const int x = x0 + i * (cardWidth + gap);
    const bool selected = mainSelection_ == i;
    canvas_.fillRoundRect(x, y0, cardWidth, cardHeight, 8,
                          selected ? kPanelSelected : kPanel);
    if (selected) canvas_.drawRoundRect(x, y0, cardWidth, cardHeight, 8, kAccent);
    // Icon: filled circle with a bold glyph.
    canvas_.fillCircle(x + cardWidth / 2, y0 + 26, 16, colors[i]);
    canvas_.setTextDatum(middle_center);
    canvas_.setTextSize(2);
    canvas_.setTextColor(TFT_BLACK, colors[i]);
    canvas_.drawString(i == 0 ? "C" : i == 1 ? "X" : "*",
                       x + cardWidth / 2, y0 + 27);
    canvas_.setTextSize(1);
    canvas_.setTextColor(selected ? TFT_WHITE : kTextDim,
                         selected ? kPanelSelected : kPanel);
    canvas_.drawString(names[i], x + cardWidth / 2, y0 + cardHeight - 14);
    canvas_.setTextDatum(top_left);
  }
  drawHint("</> select  Enter open  BtnA remote");
}

void DeviceUi::drawComingSoon() {
  canvas_.setTextDatum(middle_center);
  canvas_.setTextColor(kAccent, kBackground);
  canvas_.setTextSize(2);
  canvas_.drawString(comingAssistant_ == 0 ? "Claude" : "Codex", kWidth / 2, 52);
  canvas_.setTextSize(1);
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.drawString("Coming soon", kWidth / 2, 78);
  canvas_.setTextDatum(top_left);
  drawHint("Esc/Backspace: back");
}

void DeviceUi::drawMenuRow(int y, bool selected, const String& text,
                           const String& value) {
  const uint16_t background = selected ? kPanelSelected : kPanel;
  canvas_.fillRoundRect(4, y, kWidth - 8, 16, 3, background);
  if (selected) canvas_.drawRoundRect(4, y, kWidth - 8, 16, 3, kAccent);
  canvas_.setTextColor(selected ? TFT_WHITE : kTextDim, background);
  canvas_.setCursor(10, y + 4);
  canvas_.print(text);
  if (!value.isEmpty()) {
    const int width = canvas_.textWidth(value);
    canvas_.setCursor(kWidth - 10 - width, y + 4);
    canvas_.print(value);
  }
}

void DeviceUi::drawSettings() {
  const char* mic = settings_.micMuted ? "Muted" : "Live";
  String timeout = settings_.screenTimeoutSec == 0
      ? String("Never") : String(settings_.screenTimeoutSec) + "s";
  drawMenuRow(24, settingsSelection_ == 0, "WiFi", clipped(wifi_.currentSsid(), 10));
  drawMenuRow(42, settingsSelection_ == 1, "Computers",
              String(pairing_.pairedCount()));
  drawMenuRow(60, settingsSelection_ == 2, "Microphone", mic);
  drawMenuRow(78, settingsSelection_ == 3, "Typeless key",
              String("F") + String(settings_.typelessFunctionKey));
  drawMenuRow(96, settingsSelection_ == 4, "Brightness",
              String(settings_.brightness));
  drawMenuRow(114, settingsSelection_ == 5, "Screen off", timeout);
  drawHint("Esc: back");
}

void DeviceUi::drawWifi() {
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.setCursor(6, 24);
  canvas_.print(wifi_.scanning() ? "Scanning..." : "WiFi networks");
  const size_t count = wifi_.scanCount();
  const size_t first = listSelection_ >= 4 ? listSelection_ - 3 : 0;
  for (size_t row = 0; row < 4 && first + row < count; ++row) {
    const auto& item = wifi_.scanResult(first + row);
    String value = wifi_.connected() && wifi_.currentSsid() == item.ssid
                       ? String("<now>")
                       : (item.rssi <= -127 ? String("--") : String(item.rssi));
    if (item.saved) value += "*";
    drawMenuRow(36 + row * 18, first + row == listSelection_,
                clipped(item.ssid, 18), value);
  }
  if (!wifi_.scanning() && count == 0) {
    canvas_.setCursor(6, 60);
    canvas_.print("No 2.4GHz networks found");
  }
  drawHint("Tab rescan  Fn+Bksp forget  Esc back");
}

void DeviceUi::drawPassword() {
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.setCursor(8, 30);
  canvas_.print("Password for:");
  canvas_.setTextColor(kAccent, kBackground);
  canvas_.setCursor(8, 44);
  canvas_.print(clipped(pendingSsid_, 30));
  canvas_.drawRoundRect(6, 62, kWidth - 12, 26, 4, kTextDim);
  canvas_.setCursor(12, 71);
  canvas_.setTextColor(TFT_WHITE, kBackground);
  for (size_t i = 0; i < textEntry_.length(); ++i) canvas_.print('*');
  drawHint("Enter connect  Fn+Bksp cancel");
}

void DeviceUi::drawComputers() {
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.setCursor(6, 24);
  canvas_.print("Paired Macs");
  const size_t total = pairing_.pairedCount() + 2;
  const size_t first = listSelection_ >= 4 ? listSelection_ - 3 : 0;
  for (size_t row = 0; row < 4 && first + row < total; ++row) {
    const size_t index = first + row;
    if (index < pairing_.pairedCount()) {
      String state = pairing_.pairedCurrent(index) ? "NOW" :
                     (pairing_.pairedOnline(index) ? "online" : "offline");
      drawMenuRow(36 + row * 18, index == listSelection_,
                  clipped(pairing_.paired(index).name, 16), state);
    } else if (index == pairing_.pairedCount()) {
      drawMenuRow(36 + row * 18, index == listSelection_, "+ Add computer");
    } else {
      drawMenuRow(36 + row * 18, index == listSelection_, "< Back");
    }
  }
  drawHint("Fn+Bksp delete  Esc back");
}

void DeviceUi::drawAddComputer() {
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.setCursor(6, 24);
  canvas_.print("Nearby Macs");
  const size_t count = pairing_.discoveredCount();
  const size_t first = listSelection_ >= 4 ? listSelection_ - 3 : 0;
  for (size_t row = 0; row < 4 && first + row < count; ++row) {
    const auto& item = pairing_.discovered(first + row);
    drawMenuRow(36 + row * 18, first + row == listSelection_,
                clipped(item.name, 19), item.paired ? "paired" : "new");
  }
  if (count == 0) {
    canvas_.setCursor(6, 60);
    canvas_.print("Searching for CardBridge...");
  }
  drawHint("Tab rescan  Esc back");
}

void DeviceUi::drawPairCode() {
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.setCursor(8, 28);
  canvas_.print("Enter the 6-digit code on the Mac");
  canvas_.setTextSize(3);
  canvas_.setTextColor(kAccent, kBackground);
  canvas_.setTextDatum(middle_center);
  String code = textEntry_;
  while (code.length() < 6) code += "-";
  canvas_.drawString(code, kWidth / 2, 70);
  canvas_.setTextDatum(top_left);
  canvas_.setTextSize(1);
  drawHint("Enter pair  Fn+Bksp cancel");
}

String DeviceUi::clipped(const String& value, size_t length) const {
  if (value.length() <= length) return value;
  if (length < 2) return value.substring(0, length);
  return value.substring(0, length - 1) + "~";
}

}  // namespace cardbridge
