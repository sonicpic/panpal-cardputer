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
  if (page_ == Page::Codex &&
      (pairing_.agentFocusId() != lastAgentFocusId_ ||
       pairing_.agentFocusSeq() != lastAgentFocusSeq_)) {
    lastAgentFocusId_ = pairing_.agentFocusId();
    lastAgentFocusSeq_ = pairing_.agentFocusSeq();
    selectedAgentId_ = lastAgentFocusId_;
  }
  if (page_ == Page::Codex && !selectedAgentId_.isEmpty()) {
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
    case Page::ComingSoon: if (backPressed() || enterPressed()) setPage(Page::Main); break;
    case Page::Codex: handleCodex(); break;
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
    if (mainSelection_ == 0) {
      comingAssistant_ = mainSelection_;
      setPage(Page::ComingSoon);
    } else if (mainSelection_ == 1) {
      showCodex();
    } else {
      setPage(Page::Settings);
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

void DeviceUi::handleSettings() {
  // Mic mute and the Typeless hot-key are gone: the WiFi->BlackHole voice
  // path is a dead end (Typeless only lists real hardware mics), so both were
  // switches with nothing behind them. They come back with the USB-mic route.
  constexpr uint8_t itemCount = 4;
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
      case 2: {
        const uint8_t levels[] = {64, 128, 192, 255};
        size_t index = 0;
        while (index < 3 && settings_.brightness > levels[index]) ++index;
        settings_.brightness = levels[(index + 1) % 4];
        M5Cardputer.Display.setBrightness(settings_.brightness);
        store_.saveSettings(settings_);
        break;
      }
      case 3: {
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
  if (count > 0 && listSelection_ >= count) listSelection_ = count - 1;
  const auto& state = M5Cardputer.Keyboard.keysState();
  if (state.tab || pressed('r')) {
    wifi_.startScan();
  } else if (backspacePressed() && count > 0 &&
             wifi_.scanResult(listSelection_).saved) {
    wifi_.forget(wifi_.scanResult(listSelection_).ssid);
  } else if (escapePressed()) {
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
  if (escapePressed()) {
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
  if (backspacePressed() && listSelection_ < pairing_.pairedCount()) {
    pairing_.deletePairing(listSelection_);
    if (listSelection_ >= pairing_.pairedCount() && listSelection_ > 0) --listSelection_;
  } else if (escapePressed()) {
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
  switch (page_) {
    case Page::Main: drawMain(); break;
    case Page::ComingSoon: drawComingSoon(); break;
    case Page::Codex: drawCodex(); break;
    case Page::Settings: drawSettings(); break;
    case Page::Wifi: drawWifi(); break;
    case Page::WifiPassword: drawPassword(); break;
    case Page::Computers: drawComputers(); break;
    case Page::AddComputer: drawAddComputer(); break;
    case Page::PairCode: drawPairCode(); break;
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
    case Page::ComingSoon: return comingAssistant_ == 0 ? "Claude" : "Assistant";
    case Page::Codex: {
      const AgentSession* agent = selectedAgent();
      if (!agent) return "Codex";
      if (!agent->title.isEmpty()) return agent->title;
      if (!agent->project.isEmpty()) return agent->project;
      return "Codex";
    }
    case Page::Settings: return "Setting";
    case Page::Wifi: return "WiFi";
    case Page::WifiPassword: return "WiFi Password";
    case Page::Computers: return "Computers";
    case Page::AddComputer: return "Add Computer";
    case Page::PairCode: return "Pair Computer";
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

void DeviceUi::drawScrollingActivity(const String& activity) {
  constexpr int x = 18;
  constexpr int y = 122;
  constexpr int width = 216;
  if (activity != marqueeActivity_) {
    marqueeActivity_ = activity;
    marqueeActivityStartedMs_ = millis();
  }
  canvas_.setFont(uiFontFace_);
  canvas_.setTextColor(TFT_WHITE, kPanel);
  const int textWidth = canvas_.textWidth(activity);
  canvas_.setClipRect(x, 111, width, 23);
  canvas_.setTextDatum(middle_left);
  if (textWidth <= width) {
    canvas_.drawString(activity, x, y);
  } else {
    const uint32_t elapsed = millis() - marqueeActivityStartedMs_;
    const int travel = textWidth + 24;
    const int offset = elapsed < 1000 ? 0 : ((elapsed - 1000) / 40) % travel;
    canvas_.drawString(activity, x - offset, y);
    canvas_.drawString(activity, x - offset + travel, y);
  }
  canvas_.clearClipRect();
  canvas_.setTextDatum(top_left);
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
  static const char* names[] = {"Claude", "Codex", "Setting"};
  static const uint16_t colors[] = {kAccentWarm, kAccent, kTextDim};
  const int cardWidth = 68, cardHeight = 88, gap = 8;
  const int totalWidth = 3 * cardWidth + 2 * gap;
  const int x0 = (kWidth - totalWidth) / 2;
  const int y0 = 27;
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

const AgentSession* DeviceUi::selectedAgent() const {
  if (pairing_.agentCount() == 0 || agentSelection_ >= pairing_.agentCount()) {
    return nullptr;
  }
  return &pairing_.agent(agentSelection_);
}

void DeviceUi::drawQuotaHud(int x, int remaining, const char* label,
                            uint16_t color) {
  canvas_.setTextFont(1);
  canvas_.setTextSize(1);
  canvas_.setTextColor(remaining < 0 ? kTextDim : TFT_WHITE, kBackground);
  canvas_.setCursor(x, 25);
  canvas_.print(label);
  canvas_.setCursor(x + 38, 25);
  if (remaining < 0) {
    canvas_.print("--");
  } else {
    canvas_.printf("%d%%", remaining);
  }
  canvas_.drawRoundRect(x, 37, 72, 7, 2, kTextDim);
  if (remaining > 0) {
    const int fill = max(1, 68 * remaining / 100);
    canvas_.fillRoundRect(x + 2, 39, fill, 3, 1, color);
  }
}

void DeviceUi::drawCodex() {
  const AgentSession* agent = selectedAgent();
  PetVisualState visual = PetVisualState::Offline;
  uint16_t statusColor = kTextDim;
  String activity = pairing_.connected()
      ? "Waiting for Codex sessions..." : "CardBridge is offline";
  if (agent && pairing_.agentOnline()) {
    activity = agent->activity;
    switch (agent->status) {
      case AgentStatus::Running:
        visual = PetVisualState::Running;
        statusColor = kAccent;
        break;
      case AgentStatus::NeedsInput:
        visual = PetVisualState::NeedsInput;
        statusColor = kAccentWarm;
        break;
      case AgentStatus::Ready:
        visual = PetVisualState::Ready;
        statusColor = kGood;
        break;
      case AgentStatus::Blocked:
        visual = PetVisualState::Blocked;
        statusColor = kBad;
        break;
      case AgentStatus::Offline:
        visual = PetVisualState::Offline;
        statusColor = kTextDim;
        break;
      case AgentStatus::Idle:
        visual = PetVisualState::Idle;
        statusColor = kTextDim;
        break;
    }
  }

  // ChatGPT OAuth exposes subscription windows. API key and custom-provider
  // sessions keep the pet/status UI but omit the quota HUD entirely.
  if (pairing_.agentQuota().available) {
    const bool live = pairing_.agentOnline();
    drawQuotaHud(6, live ? pairing_.agentQuota().weeklyRemaining : -1,
                 "HP W", 0xF9E7);
    drawQuotaHud(162, live ? pairing_.agentQuota().fiveHourRemaining : -1,
                 "MP 5H", 0x05FF);
  }
  pet_.draw(canvas_, visual, 84, 28, millis());

  uint8_t attention = 0;
  for (size_t i = 0; i < pairing_.agentCount(); ++i) {
    if (i == agentSelection_) continue;
    const AgentStatus status = pairing_.agent(i).status;
    if (pairing_.agent(i).unread || status == AgentStatus::NeedsInput ||
        status == AgentStatus::Blocked) ++attention;
  }
  canvas_.setTextFont(1);
  canvas_.setTextColor(attention ? kAccentWarm : kTextDim, kBackground);
  canvas_.setCursor(6, 98);
  if (attention) canvas_.printf("!%u", attention);
  canvas_.setTextDatum(bottom_right);
  canvas_.setTextColor(kTextDim, kBackground);
  if (pairing_.agentCount()) {
    canvas_.drawString(String(agentSelection_ + 1) + "/" +
                           String(pairing_.agentCount()),
                       233, 106);
  }
  canvas_.setTextDatum(top_left);

  canvas_.fillRect(0, 110, kWidth, 25, kPanel);
  canvas_.drawFastHLine(0, 110, kWidth, 0x2945);
  canvas_.fillCircle(9, 122, 3, statusColor);
  drawScrollingActivity(activity);
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
  String timeout = settings_.screenTimeoutSec == 0
      ? String("Never") : String(settings_.screenTimeoutSec) + "s";
  drawMenuRow(30, settingsSelection_ == 0, "WiFi", clipped(wifi_.currentSsid(), 10));
  drawMenuRow(52, settingsSelection_ == 1, "Computers",
              String(pairing_.pairedCount()));
  drawMenuRow(74, settingsSelection_ == 2, "Brightness",
              String(settings_.brightness));
  drawMenuRow(96, settingsSelection_ == 3, "Screen off", timeout);
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

String DeviceUi::clipped(const String& value, size_t length) const {
  if (value.length() <= length) return value;
  if (length < 2) return value.substring(0, length);
  return value.substring(0, length - 1) + "~";
}

}  // namespace cardbridge
