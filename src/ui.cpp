#include "ui.h"

#include "ui_font_data.h"

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
constexpr int kCodexMargin = 4;
constexpr int kCodexGap = 4;
constexpr int kCodexLeftX = kCodexMargin;
constexpr int kCodexColumnWidth = 114;
constexpr int kCodexRightX = kCodexLeftX + kCodexColumnWidth + kCodexGap;
constexpr int kCodexPanelY = 4;
constexpr int kCodexPanelHeight = 127;
constexpr int kCodexPetX = 11;
constexpr int kCodexPetY = 18;
constexpr int kCodexPetSize = 100;
constexpr int kCodexKeyboardX = 6;
constexpr int kCodexKeyboardY = 6;
constexpr int kCodexSessionBadgeWidth = 24;
constexpr int kCodexSessionBadgeHeight = 12;
constexpr int kCodexSessionBadgeX =
    kCodexLeftX + (kCodexColumnWidth - kCodexSessionBadgeWidth) / 2;
constexpr int kCodexSessionBadgeY = 6;
constexpr int kCodexContentX = kCodexRightX + 4;
constexpr int kCodexContentWidth = kCodexColumnWidth - 8;
constexpr int kCodexTitleY = 8;
constexpr int kCodexTitleHeight = 18;
constexpr int kCodexActivityY = 29;
constexpr int kCodexActivityHeight = 70;
constexpr int kCodexActivityLines = 4;
// This is a native 13px face, not a fractionally scaled 15px font. Native
// advances avoid per-character rounding that made CJK spacing look cramped.
// A 17px pitch leaves four real pixels between activity lines.
constexpr float kCodexActivityTextScale = 1.0f;
constexpr int kCodexActivityTextPixels = 13;
constexpr int kCodexActivityTextInsetY = 3;
constexpr int kCodexActivityLinePitch = 17;
constexpr int kCodexWeeklyY = 103;
constexpr int kCodexFiveHourY = 116;
constexpr int kCodexQuotaRowHeight = 10;
// Low-saturation rainbow stops. Adjacent stops are interpolated per pixel so
// API quota bars flow as one calm gradient rather than flashing color blocks.
constexpr uint16_t kUnlimitedGradient[] = {
    0xD474, 0xC4DB, 0x8D7C, 0x763A, 0x8655, 0xD60F, 0xDD0F,
};
constexpr uint8_t kBrightnessLevels[] = {64, 128, 192, 255};
constexpr uint16_t kScreenTimeouts[] = {30, 60, 120, 300, 0};

static_assert(kCodexLeftX == kCodexMargin);
static_assert(kCodexRightX + kCodexColumnWidth + kCodexMargin == kWidth);
static_assert(kCodexPanelY + kCodexPanelHeight + kCodexMargin == kHeight);
static_assert(kCodexPetX >= kCodexLeftX &&
              kCodexPetX + kCodexPetSize <= kCodexLeftX + kCodexColumnWidth);
static_assert(kCodexPetY >= kCodexPanelY &&
              kCodexPetY + kCodexPetSize <= kCodexPanelY + kCodexPanelHeight);
static_assert(kCodexSessionBadgeY + kCodexSessionBadgeHeight <= kCodexPetY);
static_assert(kCodexContentX + kCodexContentWidth <=
              kCodexRightX + kCodexColumnWidth);
static_assert(kCodexTitleY + kCodexTitleHeight <= kCodexActivityY);
static_assert(kCodexActivityY + kCodexActivityHeight <= kCodexWeeklyY);
static_assert(kCodexActivityTextInsetY +
                  (kCodexActivityLines - 1) * kCodexActivityLinePitch +
                  kCodexActivityTextPixels <=
              kCodexActivityHeight);
static_assert(kCodexWeeklyY + kCodexQuotaRowHeight <= kCodexFiveHourY);
static_assert(kCodexFiveHourY + kCodexQuotaRowHeight <=
              kCodexPanelY + kCodexPanelHeight);

size_t brightnessLevelIndex(uint8_t value) {
  for (size_t i = 0; i < sizeof(kBrightnessLevels); ++i) {
    if (kBrightnessLevels[i] == value) return i;
  }
  return 1;
}

size_t utf8CharacterLength(const String& value, size_t index) {
  if (index >= value.length()) return 0;
  const uint8_t lead = static_cast<uint8_t>(value[index]);
  if ((lead & 0x80) == 0) return 1;
  const size_t remaining = value.length() - index;
  if ((lead & 0xE0) == 0xC0) return remaining < 2 ? remaining : 2;
  if ((lead & 0xF0) == 0xE0) return remaining < 3 ? remaining : 3;
  if ((lead & 0xF8) == 0xF0) return remaining < 4 ? remaining : 4;
  return 1;
}

bool asciiOnly(const String& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    if (static_cast<uint8_t>(value[i]) >= 0x80) return false;
  }
  return true;
}

void removeLastUtf8Character(String& value) {
  if (value.isEmpty()) return;
  size_t index = value.length() - 1;
  while (index > 0 &&
         (static_cast<uint8_t>(value[index]) & 0xC0) == 0x80) {
    --index;
  }
  value.remove(index);
}

uint16_t blendRgb565(uint16_t from, uint16_t to, uint8_t amount) {
  const uint16_t inverse = 255 - amount;
  const uint16_t red = (((from >> 11) & 0x1F) * inverse +
                        ((to >> 11) & 0x1F) * amount) /
                       255;
  const uint16_t green = (((from >> 5) & 0x3F) * inverse +
                          ((to >> 5) & 0x3F) * amount) /
                         255;
  const uint16_t blue = ((from & 0x1F) * inverse + (to & 0x1F) * amount) /
                        255;
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

size_t screenTimeoutIndex(uint16_t value) {
  for (size_t i = 0; i < sizeof(kScreenTimeouts) / sizeof(kScreenTimeouts[0]); ++i) {
    if (kScreenTimeouts[i] == value) return i;
  }
  return 1;
}

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
  uiFontData_.set(ui_font_data::kData, ui_font_data::size());
  if (uiFont_.loadFont(&uiFontData_)) {
    uiFontFace_ = &uiFont_;
    Serial.printf("[ui] smooth font=%u glyphs bytes=%u heap=%u\n",
                  ui_font_data::kGlyphCount,
                  static_cast<unsigned>(ui_font_data::size()), ESP.getFreeHeap());
  } else {
    uiFontFace_ = &fonts::efontCN_14;
    Serial.println("[ui] smooth font load failed; using efontCN_14");
  }
  canvas_.setTextFont(1);
  lastActivityMs_ = millis();
  render();
}

void DeviceUi::setMode(UiMode mode) {
  if (mode_ == mode) return;
  mode_ = mode;
  suppressUntilRelease_ = M5Cardputer.Keyboard.isPressed();
}

void DeviceUi::showCodex() {
  lastAgentFocusId_.clear();
  lastAgentFocusSeq_ = 0;
  selectedAgentId_.clear();
  setPage(Page::Codex);
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

  // A new user prompt moves the pet to that session. Manual left/right
  // selection remains sticky until another prompt changes focus again.
  const bool showingCodex = page_ == Page::Main || page_ == Page::Codex;
  if (showingCodex &&
      (pairing_.agentFocusId() != lastAgentFocusId_ ||
       pairing_.agentFocusSeq() != lastAgentFocusSeq_)) {
    lastAgentFocusId_ = pairing_.agentFocusId();
    lastAgentFocusSeq_ = pairing_.agentFocusSeq();
    selectedAgentId_ = lastAgentFocusId_;
  }
  if (showingCodex && !selectedAgentId_.isEmpty()) {
    bool found = false;
    for (size_t i = 0; i < pairing_.agentCount(); ++i) {
      if (pairing_.agent(i).id == selectedAgentId_) {
        agentSelection_ = i;
        found = true;
        break;
      }
    }
    if (!found) {
      if (pairing_.agentCount()) {
        agentSelection_ = min(agentSelection_, pairing_.agentCount() - 1);
        selectedAgentId_ = pairing_.agent(agentSelection_).id;
      } else {
        agentSelection_ = 0;
        selectedAgentId_.clear();
      }
    }
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
bool DeviceUi::escapePressed() const { return pressed('`'); }
bool DeviceUi::backspacePressed() const {
  return M5Cardputer.Keyboard.keysState().del;
}
bool DeviceUi::backPressed() const {
  return escapePressed() || backspacePressed();
}

void DeviceUi::handleInput() {
  switch (page_) {
    case Page::Main: handleMain(); break;
    case Page::Codex: handleCodex(); break;
    case Page::Wifi: handleWifi(); break;
    case Page::WifiPassword: handlePassword(); break;
    case Page::Computers: handleComputers(); break;
    case Page::AddComputer: handleAddComputer(); break;
    case Page::PairCode: handlePairCode(); break;
    case Page::Brightness: handleBrightness(); break;
    case Page::ScreenOff: handleScreenOff(); break;
  }
}

void DeviceUi::handleMain() {
  if (navLeft()) {
    if (mainSelection_ > 0) {
      homeSettingSelection_ = mainSelection_ - 1;
      mainSelection_ = 0;
    }
  } else if (navRight()) {
    if (mainSelection_ == 0) mainSelection_ = homeSettingSelection_ + 1;
  } else if (navUp()) {
    if (mainSelection_ == 0) {
      homeSettingSelection_ = 3;
    } else {
      homeSettingSelection_ = (mainSelection_ + 2) % 4;
    }
    mainSelection_ = homeSettingSelection_ + 1;
  } else if (navDown()) {
    if (mainSelection_ == 0) {
      homeSettingSelection_ = 0;
    } else {
      homeSettingSelection_ = mainSelection_ % 4;
    }
    mainSelection_ = homeSettingSelection_ + 1;
  } else if (enterPressed()) {
    switch (mainSelection_) {
      case 0:
        showCodex();
        break;
      case 1:
        listSelection_ = 0;
        wifi_.startScan();
        setPage(Page::Wifi);
        break;
      case 2:
        listSelection_ = 0;
        pairing_.requestDiscovery();
        setPage(Page::Computers);
        break;
      case 3:
        setPage(Page::Brightness);
        break;
      case 4:
        setPage(Page::ScreenOff);
        break;
    }
  }
}

void DeviceUi::handleCodex() {
  const size_t count = pairing_.agentCount();
  if (count > 0 && agentSelection_ >= count) {
    agentSelection_ = count - 1;
    selectedAgentId_ = pairing_.agent(agentSelection_).id;
  }
  if (backPressed()) {
    setPage(Page::Main);
  } else if (count > 0 && navLeft()) {
    agentSelection_ = (agentSelection_ + count - 1) % count;
    selectedAgentId_ = pairing_.agent(agentSelection_).id;
  } else if (count > 0 && navRight()) {
    agentSelection_ = (agentSelection_ + 1) % count;
    selectedAgentId_ = pairing_.agent(agentSelection_).id;
  } else if (count > 0 && enterPressed()) {
    pairing_.sendAgentAck(pairing_.agent(agentSelection_).id);
  }
}

void DeviceUi::handleWifi() {
  const size_t count = wifi_.scanCount();
  if (count > 0 && listSelection_ >= count) listSelection_ = count - 1;
  const auto& state = M5Cardputer.Keyboard.keysState();
  if (state.tab || pressed('r')) {
    wifi_.startScan();
  } else if (backspacePressed() && count > 0 &&
             wifi_.scanResult(listSelection_).saved) {
    wifi_.forget(wifi_.scanResult(listSelection_).ssid);
  } else if (escapePressed()) {
    setPage(Page::Main);
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
          setPage(Page::Main);
          break;
        }
      }
    } else if (selected.encrypted) {
      pendingSsid_ = selected.ssid;
      textEntry_.clear();
      setPage(Page::WifiPassword);
    } else {
      wifi_.addAndConnect(selected.ssid, "");
      setPage(Page::Main);
    }
  }
}

void DeviceUi::handlePassword() {
  const auto& state = M5Cardputer.Keyboard.keysState();
  if (escapePressed()) {
    setPage(Page::Wifi);
  } else if (state.enter) {
    wifi_.addAndConnect(pendingSsid_, textEntry_);
    textEntry_.clear();
    setPage(Page::Main);
  } else if (state.del) {
    if (!textEntry_.isEmpty()) textEntry_.remove(textEntry_.length() - 1);
  } else {
    appendTypedText(textEntry_, 63, false);
  }
}

void DeviceUi::handleComputers() {
  const size_t rows = pairing_.pairedCount() + 2;  // Add new + Back.
  if (backspacePressed() && listSelection_ < pairing_.pairedCount()) {
    pairing_.deletePairing(listSelection_);
    if (listSelection_ >= pairing_.pairedCount() && listSelection_ > 0) --listSelection_;
  } else if (escapePressed()) {
    setPage(Page::Main);
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
      setPage(Page::Main);
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
  if (escapePressed()) {
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

void DeviceUi::handleBrightness() {
  if (backPressed()) {
    setPage(Page::Main);
    return;
  }
  int direction = 0;
  if (navLeft() || navDown()) direction = -1;
  if (navRight() || navUp() || enterPressed()) direction = 1;
  if (!direction) return;
  const size_t count = sizeof(kBrightnessLevels);
  const size_t current = brightnessLevelIndex(settings_.brightness);
  const size_t next = (current + count + direction) % count;
  settings_.brightness = kBrightnessLevels[next];
  M5Cardputer.Display.setBrightness(settings_.brightness);
  store_.saveSettings(settings_);
}

void DeviceUi::handleScreenOff() {
  if (backPressed()) {
    setPage(Page::Main);
    return;
  }
  int direction = 0;
  if (navLeft() || navDown()) direction = -1;
  if (navRight() || navUp() || enterPressed()) direction = 1;
  if (!direction) return;
  const size_t count = sizeof(kScreenTimeouts) / sizeof(kScreenTimeouts[0]);
  const size_t current = screenTimeoutIndex(settings_.screenTimeoutSec);
  const size_t next = (current + count + direction) % count;
  settings_.screenTimeoutSec = kScreenTimeouts[next];
  store_.saveSettings(settings_);
  noteActivity();
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
  if (page_ != Page::Codex) drawStatusBar();
  switch (page_) {
    case Page::Main: drawMain(); break;
    case Page::Codex: drawCodex(); break;
    case Page::Wifi: drawWifi(); break;
    case Page::WifiPassword: drawPassword(); break;
    case Page::Computers: drawComputers(); break;
    case Page::AddComputer: drawAddComputer(); break;
    case Page::PairCode: drawPairCode(); break;
    case Page::Brightness: drawBrightness(); break;
    case Page::ScreenOff: drawScreenOff(); break;
  }
  // Explicit destination: the sprite's stored parent pointer is unreliable
  // when the canvas member is constructed before M5Cardputer (static-init
  // order across translation units).
  canvas_.pushSprite(&M5Cardputer.Display, 0, 0);
}

void DeviceUi::drawWifiBars(int x, int y, int rssi, bool connected) {
  drawWifiStrengthIcon(x, y, connected ? rssi : -127, kGood, kPanel);
}

void DeviceUi::drawWifiStrengthIcon(int x, int y, int rssi,
                                    uint16_t active, uint16_t inactive) {
  const int bars = rssi <= -127 ? 0 : rssi > -55 ? 4 : rssi > -65 ? 3
                                  : rssi > -75 ? 2 : 1;
  for (int i = 0; i < 4; ++i) {
    const int h = 3 + i * 3;
    canvas_.fillRect(x + i * 4, y + 12 - h, 3, h,
                     i < bars ? active : inactive);
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
  drawKeyboardModeIcon(4, 4);
  drawWifiBars(28, 4, wifi_.rssi(), wifi_.connected());
  drawScrollingTitle(statusBarTitle());
  drawBattery(214, 5);
}

String DeviceUi::statusBarTitle() const {
  switch (page_) {
    case Page::Main: return "CardBridge";
    case Page::Codex: {
      const AgentSession* agent = selectedAgent();
      if (!agent) return "Codex";
      if (!agent->title.isEmpty()) return agent->title;
      if (!agent->project.isEmpty()) return agent->project;
      return "Codex";
    }
    case Page::Wifi: return "WiFi";
    case Page::WifiPassword: return "WiFi Password";
    case Page::Computers: return "Computers";
    case Page::AddComputer: return "Add Computer";
    case Page::PairCode: return "Pair Computer";
    case Page::Brightness: return "Brightness";
    case Page::ScreenOff: return "Screen off";
  }
  return "CardBridge";
}

void DeviceUi::drawScrollingTitle(const String& title) {
  constexpr int x = 48;
  constexpr int width = 144;
  if (title != marqueeTitle_) {
    marqueeTitle_ = title;
    marqueeStartedMs_ = millis();
  }
  canvas_.setFont(uiFontFace_);
  canvas_.setTextColor(TFT_WHITE, TFT_BLACK);
  const int textWidth = canvas_.textWidth(title);
  canvas_.setClipRect(x, 0, width, kStatusHeight);
  canvas_.setTextDatum(middle_left);
  if (textWidth <= width) {
    canvas_.drawString(title, x + (width - textWidth) / 2, 10);
  } else {
    const uint32_t elapsed = millis() - marqueeStartedMs_;
    const int travel = textWidth + 28;
    const int offset = elapsed < 1200 ? 0 : ((elapsed - 1200) / 40) % travel;
    canvas_.drawString(title, x - offset, 10);
    canvas_.drawString(title, x - offset + travel, 10);
  }
  canvas_.clearClipRect();
  canvas_.setTextDatum(top_left);
  canvas_.setTextFont(1);
}

void DeviceUi::drawCodexTitle(const String& title) {
  if (title != marqueeTitle_) {
    marqueeTitle_ = title;
    marqueeStartedMs_ = millis();
  }
  canvas_.setFont(uiFontFace_);
  canvas_.setTextSize(1.0f);
  canvas_.setTextColor(TFT_WHITE, kBackground);
  const int textWidth = canvas_.textWidth(title);
  canvas_.setClipRect(kCodexContentX, kCodexTitleY, kCodexContentWidth,
                      kCodexTitleHeight);
  canvas_.setTextDatum(middle_left);
  if (textWidth <= kCodexContentWidth) {
    canvas_.drawString(title, kCodexContentX,
                       kCodexTitleY + kCodexTitleHeight / 2);
  } else {
    const uint32_t elapsed = millis() - marqueeStartedMs_;
    const int travel = textWidth + 18;
    const int offset = elapsed < 1000 ? 0 : ((elapsed - 1000) / 45) % travel;
    canvas_.drawString(title, kCodexContentX - offset,
                       kCodexTitleY + kCodexTitleHeight / 2);
    canvas_.drawString(title, kCodexContentX - offset + travel,
                       kCodexTitleY + kCodexTitleHeight / 2);
  }
  canvas_.clearClipRect();
  canvas_.setTextDatum(top_left);
  canvas_.setTextFont(1);
}

void DeviceUi::drawCodexSessionBadge(size_t index, size_t count) {
  if (count == 0) return;
  canvas_.fillRoundRect(kCodexSessionBadgeX, kCodexSessionBadgeY,
                        kCodexSessionBadgeWidth, kCodexSessionBadgeHeight, 3,
                        kBackground);
  canvas_.drawRoundRect(kCodexSessionBadgeX, kCodexSessionBadgeY,
                        kCodexSessionBadgeWidth, kCodexSessionBadgeHeight, 3,
                        kTextDim);
  canvas_.setTextFont(1);
  canvas_.setTextSize(1);
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.setTextDatum(middle_center);
  canvas_.drawString(String(index + 1) + "/" + String(count),
                     kCodexSessionBadgeX + kCodexSessionBadgeWidth / 2,
                     kCodexSessionBadgeY + kCodexSessionBadgeHeight / 2);
  canvas_.setTextDatum(top_left);
}

String DeviceUi::fitWithEllipsis(String text, int width, bool force) {
  canvas_.setFont(uiFontFace_);
  if (!force && canvas_.textWidth(text) <= width) return text;
  const String suffix = "...";
  while (!text.isEmpty() && canvas_.textWidth(text + suffix) > width) {
    removeLastUtf8Character(text);
  }
  return text + suffix;
}

void DeviceUi::prepareCodexActivity(const String& activity) {
  if (activity == wrappedActivity_) return;
  wrappedActivity_ = activity;
  activityLineCount_ = 0;
  for (String& line : activityLines_) line.clear();
  canvas_.setFont(uiFontFace_);
  canvas_.setTextSize(kCodexActivityTextScale);

  bool overflow = false;
  String current;
  auto pushLine = [&](const String& line) {
    if (line.isEmpty()) return true;
    if (activityLineCount_ >= kCodexActivityLines) {
      overflow = true;
      return false;
    }
    activityLines_[activityLineCount_++] = line;
    return true;
  };

  size_t cursor = 0;
  while (cursor < activity.length() && !overflow) {
    while (cursor < activity.length() &&
           isspace(static_cast<unsigned char>(activity[cursor]))) {
      ++cursor;
    }
    const size_t start = cursor;
    while (cursor < activity.length() &&
           !isspace(static_cast<unsigned char>(activity[cursor]))) {
      ++cursor;
    }
    if (start == cursor) continue;
    const String token = activity.substring(start, cursor);
    const String candidate = current.isEmpty() ? token : current + " " + token;
    if (canvas_.textWidth(candidate) <= kCodexContentWidth) {
      current = candidate;
      continue;
    }

    if (!current.isEmpty()) {
      if (!pushLine(current)) break;
      current.clear();
    }
    if (canvas_.textWidth(token) <= kCodexContentWidth) {
      current = token;
      continue;
    }
    if (asciiOnly(token)) {
      if (!pushLine(fitWithEllipsis(token, kCodexContentWidth, false))) break;
      continue;
    }

    // Chinese may wrap at character boundaries, but an embedded ASCII run is
    // kept intact so names such as CardBridge never split letter by letter.
    String chunk;
    for (size_t index = 0; index < token.length() && !overflow;) {
      size_t end = index;
      if (static_cast<uint8_t>(token[index]) < 0x80) {
        while (end < token.length() &&
               static_cast<uint8_t>(token[end]) < 0x80) {
          ++end;
        }
      } else {
        end += utf8CharacterLength(token, index);
      }
      const String segment = token.substring(index, end);
      if (canvas_.textWidth(segment) > kCodexContentWidth) {
        if (!chunk.isEmpty() && !pushLine(chunk)) break;
        chunk.clear();
        if (!pushLine(fitWithEllipsis(segment, kCodexContentWidth, false))) break;
        index = end;
        continue;
      }
      const String expanded = chunk + segment;
      if (!chunk.isEmpty() &&
          canvas_.textWidth(expanded) > kCodexContentWidth) {
        if (!pushLine(chunk)) break;
        chunk = segment;
      } else {
        chunk = expanded;
      }
      index = end;
    }
    current = chunk;
  }

  if (!overflow && !current.isEmpty()) pushLine(current);
  if (cursor < activity.length()) overflow = true;
  if (overflow && activityLineCount_ > 0) {
    const uint8_t last = activityLineCount_ - 1;
    activityLines_[last] =
        fitWithEllipsis(activityLines_[last], kCodexContentWidth, true);
  }
  canvas_.setTextSize(1.0f);
  canvas_.setTextFont(1);
}

void DeviceUi::drawCodexActivity(const String& activity) {
  prepareCodexActivity(activity);
  canvas_.setFont(uiFontFace_);
  canvas_.setTextSize(kCodexActivityTextScale);
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.setTextDatum(top_left);
  canvas_.setClipRect(kCodexContentX, kCodexActivityY, kCodexContentWidth,
                      kCodexActivityHeight);
  for (uint8_t line = 0; line < activityLineCount_; ++line) {
    canvas_.drawString(activityLines_[line], kCodexContentX,
                       kCodexActivityY + kCodexActivityTextInsetY +
                           line * kCodexActivityLinePitch);
  }
  canvas_.clearClipRect();
  canvas_.setTextSize(1.0f);
  canvas_.setTextFont(1);
}

void DeviceUi::drawKeyboardModeIcon(int x, int y) {
  const bool enabled = mode_ == UiMode::Remote;
  const uint16_t color = enabled ? kAccent : kTextDim;
  if (enabled) {
    canvas_.fillRoundRect(x, y, 18, 12, 2, color);
  } else {
    canvas_.drawRoundRect(x, y, 18, 12, 2, color);
  }
  const uint16_t keyColor = enabled ? TFT_BLACK : color;
  for (int column = 0; column < 4; ++column) {
    canvas_.fillRect(x + 3 + column * 3, y + 3, 2, 2, keyColor);
  }
  canvas_.fillRect(x + 3, y + 7, 12, 2, keyColor);
  if (!enabled) canvas_.drawLine(x + 2, y + 11, x + 16, y + 1, kBad);
}

void DeviceUi::drawHint(const String& text) {
  canvas_.setTextSize(1);
  canvas_.setTextColor(kTextDim, kBackground);
  canvas_.setTextDatum(bottom_center);
  canvas_.drawString(text, kWidth / 2, kHeight - 3);
  canvas_.setTextDatum(top_left);
}

void DeviceUi::drawMain() {
  constexpr int cardX = 4;
  constexpr int cardY = 24;
  constexpr int cardWidth = 108;
  constexpr int cardHeight = 105;
  const bool selected = mainSelection_ == 0;
  const uint16_t background = selected ? kPanelSelected : kPanel;
  canvas_.fillRoundRect(cardX, cardY, cardWidth, cardHeight, 7, background);
  if (selected) {
    canvas_.drawRoundRect(cardX, cardY, cardWidth, cardHeight, 7, kAccent);
  }

  pet_.draw(canvas_, codexVisualState(), 22, 25, millis());
  canvas_.setTextFont(1);
  canvas_.setTextSize(1);
  canvas_.setTextDatum(middle_center);
  canvas_.setTextColor(selected ? TFT_WHITE : kTextDim, background);
  canvas_.drawString("Codex", cardX + cardWidth / 2, 104);
  canvas_.fillCircle(13, 119, 3, codexStatusColor());
  canvas_.setTextDatum(middle_left);
  canvas_.drawString(clipped(codexPreviewStatus(), 13), 21, 119);
  canvas_.setTextDatum(top_left);

  const String wifiValue = wifi_.connected()
      ? clipped(wifi_.currentSsid(), 8) : String("offline");
  const String computerValue = pairing_.connected()
      ? String("online") : String(pairing_.pairedCount());
  const String timeoutValue = settings_.screenTimeoutSec == 0
      ? String("Never") : String(settings_.screenTimeoutSec) + "s";
  drawHomeSettingRow(24, 0, "WiFi", wifiValue);
  drawHomeSettingRow(51, 1, "Computers", computerValue);
  drawHomeSettingRow(78, 2, "Brightness", String(settings_.brightness));
  drawHomeSettingRow(105, 3, "Screen off", timeoutValue);
}

void DeviceUi::drawHomeSettingRow(int y, uint8_t index, const String& text,
                                  const String& value) {
  constexpr int x = 118;
  constexpr int width = 118;
  constexpr int height = 23;
  const bool selected = mainSelection_ == index + 1;
  const uint16_t background = selected ? kPanelSelected : kPanel;
  canvas_.fillRoundRect(x, y, width, height, 5, background);
  if (selected) canvas_.drawRoundRect(x, y, width, height, 5, kAccent);
  canvas_.setTextFont(1);
  canvas_.setTextSize(1);
  canvas_.setTextColor(selected ? TFT_WHITE : kTextDim, background);
  canvas_.setCursor(x + 7, y + 8);
  canvas_.print(text);
  if (!value.isEmpty()) {
    const int valueWidth = canvas_.textWidth(value);
    canvas_.setTextColor(selected ? TFT_WHITE : kAccent, background);
    canvas_.setCursor(x + width - valueWidth - 6, y + 8);
    canvas_.print(value);
  }
}

const AgentSession* DeviceUi::selectedAgent() const {
  if (pairing_.agentCount() == 0 || agentSelection_ >= pairing_.agentCount()) {
    return nullptr;
  }
  return &pairing_.agent(agentSelection_);
}

PetVisualState DeviceUi::codexVisualState() const {
  if (!pairing_.connected()) return PetVisualState::Offline;
  const AgentSession* agent = selectedAgent();
  if (!agent || !pairing_.agentOnline()) return PetVisualState::Idle;
  switch (agent->status) {
    case AgentStatus::Running:
      return agent->phase == AgentPhase::Tool
          ? PetVisualState::Running : PetVisualState::Thinking;
    case AgentStatus::NeedsInput: return PetVisualState::NeedsInput;
    case AgentStatus::Ready: return PetVisualState::Ready;
    case AgentStatus::Blocked: return PetVisualState::Blocked;
    case AgentStatus::Offline: return PetVisualState::Offline;
    case AgentStatus::Idle: return PetVisualState::Idle;
  }
  return PetVisualState::Idle;
}

uint16_t DeviceUi::codexStatusColor() const {
  switch (codexVisualState()) {
    case PetVisualState::Running:
    case PetVisualState::Thinking: return kAccent;
    case PetVisualState::NeedsInput: return kAccentWarm;
    case PetVisualState::Ready: return kGood;
    case PetVisualState::Blocked: return kBad;
    case PetVisualState::Offline:
    case PetVisualState::Idle: return kTextDim;
  }
  return kTextDim;
}

String DeviceUi::codexPreviewStatus() const {
  if (!pairing_.connected()) return "Mac offline";
  const AgentSession* agent = selectedAgent();
  if (!pairing_.agentOnline()) return "Waiting";
  if (!agent) return "No sessions";
  switch (agent->status) {
    case AgentStatus::Running:
      return agent->phase == AgentPhase::Tool ? "Running" : "Thinking";
    case AgentStatus::NeedsInput: return "Needs input";
    case AgentStatus::Ready: return "Ready";
    case AgentStatus::Blocked: return "Blocked";
    case AgentStatus::Offline: return "Offline";
    case AgentStatus::Idle: return "Idle";
  }
  return "Idle";
}

void DeviceUi::drawQuotaRow(int y, const char* label, int remaining,
                            uint16_t color, AgentQuotaMode mode) {
  canvas_.setTextFont(1);
  canvas_.setTextSize(1);
  canvas_.setTextColor(mode == AgentQuotaMode::Unknown ? kTextDim : TFT_WHITE,
                       kPanel);
  canvas_.setCursor(kCodexContentX, y + 2);
  canvas_.print(label);
  constexpr int barX = 164;
  constexpr int barWidth = 68;
  constexpr int barHeight = 8;
  const int barY = y + (kCodexQuotaRowHeight - barHeight) / 2;
  canvas_.fillRoundRect(barX, barY, barWidth, barHeight, 2, kBackground);
  if (mode == AgentQuotaMode::Api) {
    constexpr size_t colorCount =
        sizeof(kUnlimitedGradient) / sizeof(kUnlimitedGradient[0]);
    constexpr int stopWidth = 12;
    constexpr int gradientWidth = colorCount * stopWidth;
    // One-pixel movement every 120ms gives the full gradient a roughly
    // ten-second cycle. There is deliberately no sparkle/glint overlay.
    const int phase = (millis() / 120) % gradientWidth;
    const int innerWidth = barWidth - 2;
    for (int column = 0; column < innerWidth; ++column) {
      const int position = (phase + column) % gradientWidth;
      const size_t colorIndex = position / stopWidth;
      const size_t nextColor = (colorIndex + 1) % colorCount;
      const uint8_t blend = static_cast<uint8_t>(
          (position % stopWidth) * 255 / stopWidth);
      const uint16_t color = blendRgb565(kUnlimitedGradient[colorIndex],
                                         kUnlimitedGradient[nextColor], blend);
      const uint16_t edge = blendRgb565(kBackground, color, 176);
      canvas_.drawPixel(barX + 1 + column, barY + 1, edge);
      canvas_.drawFastVLine(barX + 1 + column, barY + 2, barHeight - 4, color);
      canvas_.drawPixel(barX + 1 + column, barY + barHeight - 2, edge);
    }
    // Draw two complete loops with a dark halo. This remains legible inside
    // the compact row and cannot be vertically clipped like a font glyph.
    const int centerX = barX + barWidth / 2;
    const int centerY = barY + barHeight / 2;
    canvas_.drawEllipse(centerX - 3, centerY, 5, 3, kBackground);
    canvas_.drawEllipse(centerX + 3, centerY, 5, 3, kBackground);
    canvas_.drawEllipse(centerX - 3, centerY, 4, 2, TFT_WHITE);
    canvas_.drawEllipse(centerX + 3, centerY, 4, 2, TFT_WHITE);
  } else if (mode == AgentQuotaMode::Subscription && remaining >= 0) {
    if (remaining > 0) {
      const int fill = max(1, (barWidth - 2) * remaining / 100);
      canvas_.fillRoundRect(barX + 1, barY + 1, fill, barHeight - 2, 1,
                            color);
    }
  } else {
    canvas_.setTextColor(kTextDim, kBackground);
    canvas_.setTextDatum(middle_center);
    canvas_.drawString("--", barX + barWidth / 2, barY + barHeight / 2);
    canvas_.setTextDatum(top_left);
  }
  canvas_.drawRoundRect(barX, barY, barWidth, barHeight, 2, kTextDim);
}

void DeviceUi::drawCodex() {
  const AgentSession* agent = selectedAgent();
  const PetVisualState visual = codexVisualState();
  const uint16_t statusColor = codexStatusColor();
  String title = "Codex";
  String activity = pairing_.connected()
      ? "Waiting for Codex sessions" : "CardBridge is offline";
  if (agent && pairing_.agentOnline()) {
    title = !agent->title.isEmpty() ? agent->title
                                   : (!agent->project.isEmpty() ? agent->project
                                                               : String("Codex"));
    activity = agent->activity;
  }

  // Final 1:1 layout: two 114px columns separated by four pixels.
  canvas_.fillRoundRect(kCodexLeftX, kCodexPanelY, kCodexColumnWidth,
                        kCodexPanelHeight, 7, kPanel);
  canvas_.fillRoundRect(kCodexRightX, kCodexPanelY, kCodexColumnWidth,
                        kCodexPanelHeight, 7, kPanel);

  pet_.draw(canvas_, visual, kCodexPetX, kCodexPetY, millis(), kCodexPetSize);
  drawKeyboardModeIcon(kCodexKeyboardX, kCodexKeyboardY);
  drawCodexSessionBadge(agentSelection_, pairing_.agentCount());

  canvas_.fillRoundRect(kCodexContentX - 2, kCodexTitleY - 2,
                        kCodexContentWidth + 4, kCodexTitleHeight + 4, 5,
                        kBackground);
  canvas_.fillRoundRect(kCodexContentX - 2, kCodexActivityY - 1,
                        kCodexContentWidth + 4, kCodexActivityHeight + 3, 5,
                        kBackground);
  canvas_.drawRoundRect(kCodexContentX - 2, kCodexActivityY - 1,
                        kCodexContentWidth + 4, kCodexActivityHeight + 3, 5,
                        statusColor);
  drawCodexTitle(title);
  drawCodexActivity(activity);

  AgentQuotaMode quotaMode = pairing_.agentOnline()
                                 ? pairing_.agentQuota().mode
                                 : AgentQuotaMode::Unknown;
  drawQuotaRow(kCodexWeeklyY, "WEEKLY", pairing_.agentQuota().weeklyRemaining,
               0xF9E7, quotaMode);
  drawQuotaRow(kCodexFiveHourY, "5H", pairing_.agentQuota().fiveHourRemaining,
               0x05FF, quotaMode);
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

void DeviceUi::drawWifi() {
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.setCursor(6, 24);
  canvas_.print(wifi_.scanning() ? "Scanning..." : "WiFi networks");
  const size_t count = wifi_.scanCount();
  const size_t first = listSelection_ >= 4 ? listSelection_ - 3 : 0;
  for (size_t row = 0; row < 4 && first + row < count; ++row) {
    const auto& item = wifi_.scanResult(first + row);
    const int y = 36 + row * 18;
    const bool selected = first + row == listSelection_;
    const uint16_t background = selected ? kPanelSelected : kPanel;
    drawMenuRow(y, selected, clipped(item.ssid, 18));
    drawWifiStrengthIcon(184, y + 2, item.rssi,
                         selected ? TFT_WHITE : kGood, background);
    if (item.saved) {
      const uint16_t color = selected ? TFT_WHITE : kTextDim;
      canvas_.drawRoundRect(204, y + 7, 9, 7, 1, color);
      canvas_.drawRect(206, y + 4, 5, 5, color);
    }
    if (wifi_.connected() && wifi_.currentSsid() == item.ssid) {
      canvas_.fillCircle(225, y + 8, 3, kGood);
    }
  }
  if (!wifi_.scanning() && count == 0) {
    canvas_.setCursor(6, 60);
    canvas_.print("No 2.4GHz networks found");
  }
  drawHint("Bksp forget  Tab rescan  Esc back");
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
  const bool shifted = M5Cardputer.Keyboard.keysState().shift;
  canvas_.setTextColor(shifted ? kAccent : kTextDim, kBackground);
  canvas_.setCursor(8, 98);
  canvas_.print(shifted ? "SHIFT: UPPERCASE / SYMBOLS"
                        : "Shift supports A-Z and symbols");
  drawHint("Enter connect  Esc cancel");
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
  drawHint("Bksp delete  Esc back");
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
  drawHint("Enter pair  Esc cancel");
}

void DeviceUi::drawBrightness() {
  canvas_.setTextDatum(middle_center);
  canvas_.setTextSize(3);
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.drawString(String(settings_.brightness), kWidth / 2, 53);
  canvas_.setTextSize(1);
  canvas_.setTextColor(kTextDim, kBackground);
  canvas_.drawString("Display brightness", kWidth / 2, 76);

  canvas_.drawRoundRect(28, 88, 184, 12, 3, kTextDim);
  const int fill = max(3, 178 * settings_.brightness / 255);
  canvas_.fillRoundRect(31, 91, fill, 6, 2, kAccent);
  const size_t selected = brightnessLevelIndex(settings_.brightness);
  for (size_t i = 0; i < sizeof(kBrightnessLevels); ++i) {
    canvas_.fillCircle(64 + i * 37, 110, 3,
                       i == selected ? kAccent : kPanel);
  }
  canvas_.setTextDatum(top_left);
  drawHint("Left/right adjust  Esc back");
}

void DeviceUi::drawScreenOff() {
  const String value = settings_.screenTimeoutSec == 0
      ? String("Never") : String(settings_.screenTimeoutSec) + " sec";
  canvas_.setTextDatum(middle_center);
  canvas_.setTextSize(3);
  canvas_.setTextColor(TFT_WHITE, kBackground);
  canvas_.drawString(value, kWidth / 2, 55);
  canvas_.setTextSize(1);
  canvas_.setTextColor(kTextDim, kBackground);
  canvas_.drawString("Automatic screen off", kWidth / 2, 78);

  const size_t selected = screenTimeoutIndex(settings_.screenTimeoutSec);
  static const char* labels[] = {"30", "60", "120", "300", "Never"};
  for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
    const int x = 8 + i * 46;
    const bool active = i == selected;
    canvas_.fillRoundRect(x, 91, 40, 18, 4,
                          active ? kPanelSelected : kPanel);
    if (active) canvas_.drawRoundRect(x, 91, 40, 18, 4, kAccent);
    canvas_.setTextColor(active ? TFT_WHITE : kTextDim,
                         active ? kPanelSelected : kPanel);
    canvas_.drawString(labels[i], x + 20, 100);
  }
  canvas_.setTextDatum(top_left);
  drawHint("Left/right adjust  Esc back");
}

String DeviceUi::clipped(const String& value, size_t length) const {
  if (value.length() <= length) return value;
  if (length < 2) return value.substring(0, length);
  return value.substring(0, length - 1) + "~";
}

}  // namespace cardbridge
