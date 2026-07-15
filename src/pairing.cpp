#include "pairing.h"

namespace cardbridge {

void PairingManager::begin(DeviceSettings* settings) {
  settings_ = settings;
  pairedCount_ = store_.loadPairedMacs(paired_, kMaxPairedMacs);
  deviceId_ = WiFi.macAddress();
  deviceId_.replace(":", "");
  deviceId_.toLowerCase();
  incoming_.reserve(4096);
  if (settings_ && !settings_->lastMacId.isEmpty()) targetId_ = settings_->lastMacId;
}

void PairingManager::tick() {
  if (!wifi_.connected()) {
    if (client_.connected()) client_.stop();
    state_ = LinkState::Offline;
    agentOnline_ = false;
    setAudioReady(false);
    if (mdnsStarted_) MDNS.end();
    mdnsStarted_ = false;
    return;
  }

  if (!mdnsStarted_) {
    mdnsStarted_ = MDNS.begin((String("cardputer-") + deviceId_.substring(6)).c_str());
    discoveryRequested_ = true;
  }

  if (discoveryRequested_) discover();

  if (client_.connected()) {
    readIncoming();
    const uint32_t now = millis();
    if (now - lastHeartbeatMs_ >= kHeartbeatMs) {
      StaticJsonDocument<256> ping;
      ping["t"] = "ping";
      if (!sendDocument(ping) || ++missedPongs_ >= kHeartbeatMissLimit) {
        connectionLost();
        return;
      }
      lastHeartbeatMs_ = now;
    }
    return;
  }

  if (state_ == LinkState::Connected || state_ == LinkState::Authenticating ||
      state_ == LinkState::AwaitingPairCode) {
    connectionLost();
  }

  if (!manualDisconnect_ && !targetId_.isEmpty() &&
      static_cast<int32_t>(millis() - nextConnectMs_) >= 0) {
    int found = discoveredIndexById(targetId_);
    if (found >= 0) {
      targetIp_ = discovered_[found].ip;
      targetPort_ = discovered_[found].port;
      targetName_ = discovered_[found].name;
      attemptConnection();
    } else {
      discoveryRequested_ = true;
      nextConnectMs_ = millis() + reconnectDelayMs_;
    }
  }
}

void PairingManager::requestDiscovery() {
  if (wifi_.connected()) discoveryRequested_ = true;
}

void PairingManager::discover() {
  discoveryRequested_ = false;
  state_ = client_.connected() ? state_ : LinkState::Discovering;
  const int count = MDNS.queryService("cardbridge", "tcp");
  discoveredCount_ = 0;
  for (int i = 0; i < count && discoveredCount_ < kMaxDiscoveredMacs; ++i) {
    DiscoveredMac mac;
    mac.id = MDNS.txt(i, "id");
    mac.name = MDNS.txt(i, "name");
    if (mac.id.isEmpty()) mac.id = MDNS.hostname(i);
    if (mac.name.isEmpty()) mac.name = MDNS.hostname(i);
    mac.ip = MDNS.address(i);  // renamed from IP(i) in Arduino core 3.x
    // The ESP32 mDNS resolver sometimes returns the service without its A
    // record (0.0.0.0). Keeping such an entry would wedge the reconnect
    // machine on an unconnectable address; drop it and let a later
    // discovery sweep resolve it properly.
    if (mac.ip == IPAddress()) continue;
    mac.port = MDNS.port(i) ? MDNS.port(i) : kControlPort;
    mac.paired = pairedIndexById(mac.id) >= 0;
    discovered_[discoveredCount_++] = mac;
  }

  if (!client_.connected()) state_ = LinkState::Offline;
  if (!manualDisconnect_ && !targetId_.isEmpty()) {
    const int found = discoveredIndexById(targetId_);
    if (found >= 0 && static_cast<int32_t>(millis() - nextConnectMs_) >= 0) {
      targetIp_ = discovered_[found].ip;
      targetPort_ = discovered_[found].port;
      targetName_ = discovered_[found].name;
    }
  }
}

bool PairingManager::connectToDiscovered(size_t index) {
  if (index >= discoveredCount_) return false;
  disconnect(false);
  manualDisconnect_ = false;
  targetId_ = discovered_[index].id;
  targetName_ = discovered_[index].name;
  targetIp_ = discovered_[index].ip;
  targetPort_ = discovered_[index].port;
  const int pairedIndex = pairedIndexById(targetId_);
  targetToken_ = pairedIndex >= 0 ? paired_[pairedIndex].token : String();
  nextConnectMs_ = 0;
  reconnectDelayMs_ = kReconnectMinMs;
  return true;
}

bool PairingManager::connectToPaired(size_t index) {
  if (index >= pairedCount_) return false;
  const int discoveredIndex = discoveredIndexById(paired_[index].id);
  if (discoveredIndex < 0) return false;
  return connectToDiscovered(static_cast<size_t>(discoveredIndex));
}

void PairingManager::attemptConnection() {
  if (targetIp_ == IPAddress()) {
    // No usable address yet — back off and re-discover instead of silently
    // spinning against 0.0.0.0.
    scheduleReconnect();
    return;
  }
  state_ = LinkState::Connecting;
  client_.stop();  // Enforce exactly one active Mac connection.
  if (!client_.connect(targetIp_, targetPort_, 1500)) {
    scheduleReconnect();
    state_ = LinkState::Offline;
    return;
  }
  client_.setNoDelay(true);
  client_.setTimeout(1);
  incoming_.clear();
  missedPongs_ = 0;
  lastHeartbeatMs_ = millis();
  sendHello();
}

void PairingManager::sendHello() {
  const int index = pairedIndexById(targetId_);
  targetToken_ = index >= 0 ? paired_[index].token : String();
  StaticJsonDocument<256> hello;
  hello["t"] = "hello";
  hello["dev_id"] = deviceId_;
  if (targetToken_.isEmpty()) {
    hello["token"] = nullptr;
  } else {
    hello["token"] = targetToken_;
  }
  if (!sendDocument(hello)) {
    connectionLost();
  } else {
    state_ = targetToken_.isEmpty() ? LinkState::AwaitingPairCode
                                    : LinkState::Authenticating;
  }
}

bool PairingManager::submitPairCode(const String& sixDigits) {
  if (!client_.connected() || sixDigits.length() != 6) return false;
  for (char c : sixDigits) {
    if (!isDigit(c)) return false;
  }
  StaticJsonDocument<128> pair;
  pair["t"] = "pair";
  pair["code"] = sixDigits;
  state_ = LinkState::Authenticating;
  return sendDocument(pair);
}

bool PairingManager::sendKey(const char* key, const char* action, bool cmd,
                             bool shift, bool option, bool control) {
  if (!connected() || !client_.connected()) return false;
  StaticJsonDocument<384> document;
  document["t"] = "key";
  document["k"] = key;
  document["a"] = action;
  JsonArray modifiers = document.createNestedArray("m");
  if (cmd) modifiers.add("cmd");
  if (shift) modifiers.add("shift");
  if (option) modifiers.add("alt");
  if (control) modifiers.add("ctrl");
  return sendDocument(document);
}

bool PairingManager::sendAgentAck(const String& sessionId) {
  if (!connected() || sessionId.isEmpty()) return false;
  StaticJsonDocument<320> document;
  document["t"] = "agent_ack";
  document["id"] = sessionId;
  return sendDocument(document);
}

bool PairingManager::sendDocument(JsonDocument& document) {
  if (!client_.connected()) return false;
  if (state_ == LinkState::Connected && targetToken_.length() == 64 &&
      !document["token"].is<String>()) {
    document["token"] = targetToken_;
  }
  if (document.overflowed()) return false;
  char line[512];
  const size_t jsonLength = measureJson(document);
  if (jsonLength + 1 > sizeof(line)) return false;
  const size_t written = serializeJson(document, line, sizeof(line));
  line[written] = '\n';
  return client_.write(reinterpret_cast<const uint8_t*>(line), written + 1) ==
         written + 1;
}

void PairingManager::readIncoming() {
  while (client_.available()) {
    const char c = static_cast<char>(client_.read());
    if (c == '\n') {
      if (!incoming_.isEmpty()) handleLine(incoming_);
      incoming_.clear();
    } else if (c != '\r') {
      if (incoming_.length() < 4096) {
        incoming_ += c;
      } else {
        incoming_.clear();  // Reject oversized lines without losing the link.
      }
    }
  }
}

void PairingManager::handleLine(const String& line) {
  incomingDocument_.clear();
  if (deserializeJson(incomingDocument_, line) != DeserializationError::Ok) return;
  const String type = incomingDocument_["t"].as<String>();
  if (state_ == LinkState::Connected &&
      (type == "ping" || type == "pong" || type == "agent_status" ||
       type == "agent_list") &&
      incomingDocument_["token"].as<String>() != targetToken_) {
    return;
  }
  if (type == "pong") {
    missedPongs_ = 0;
    return;
  }
  if (type == "ping") {
    StaticJsonDocument<256> pong;
    pong["t"] = "pong";
    sendDocument(pong);
    return;
  }
  if (type == "pair_required") {
    targetName_ = incomingDocument_["mac_name"] | targetName_;
    connectedName_ = targetName_;
    state_ = LinkState::AwaitingPairCode;
    return;
  }
  if (type == "pair_error") {
    state_ = LinkState::AwaitingPairCode;
    return;
  }
  if (type == "paired") {
    const String token = incomingDocument_["token"].as<String>();
    if (token.length() < 64) {
      connectionLost();
      return;
    }
    targetToken_ = token;
    int index = pairedIndexById(targetId_);
    if (index < 0) {
      if (pairedCount_ >= kMaxPairedMacs) {
        connectionLost();
        return;
      }
      index = static_cast<int>(pairedCount_++);
    }
    paired_[index].id = targetId_;
    paired_[index].name = incomingDocument_["mac_name"] | targetName_;
    paired_[index].token = token;
    store_.savePairedMacs(paired_, pairedCount_);
    connectedName_ = paired_[index].name;
    state_ = LinkState::Connected;
    setAudioReady(true);
    reconnectDelayMs_ = kReconnectMinMs;
    requestAgentList();
    if (settings_) {
      settings_->lastMacId = targetId_;
      persistSettings();
    }
    return;
  }
  if (type == "hello_ok") {
    connectedName_ = incomingDocument_["mac_name"] | targetName_;
    state_ = LinkState::Connected;
    setAudioReady(true);
    reconnectDelayMs_ = kReconnectMinMs;
    requestAgentList();
    if (settings_) {
      settings_->lastMacId = targetId_;
      persistSettings();
    }
    return;
  }
  if (type == "auth_error") {
    const int index = pairedIndexById(targetId_);
    if (index >= 0) {
      paired_[index].token.clear();
      store_.savePairedMacs(paired_, pairedCount_);
    }
    targetToken_.clear();
    state_ = LinkState::AwaitingPairCode;
    sendHello();
    return;
  }
  if (type == "agent_status" || type == "agent_list") {
    parseAgentSnapshot(incomingDocument_);
    return;
  }
  // Every future/unknown type is deliberately ignored.
}

namespace {

AgentStatus parseAgentStatus(const char* value) {
  if (!value) return AgentStatus::Idle;
  if (!strcmp(value, "running")) return AgentStatus::Running;
  if (!strcmp(value, "needs_input")) return AgentStatus::NeedsInput;
  if (!strcmp(value, "ready")) return AgentStatus::Ready;
  if (!strcmp(value, "blocked")) return AgentStatus::Blocked;
  if (!strcmp(value, "offline")) return AgentStatus::Offline;
  return AgentStatus::Idle;
}

int8_t quotaRemaining(JsonVariantConst window) {
  if (window.isNull() || !window["remaining"].is<int>()) return -1;
  return static_cast<int8_t>(constrain(window["remaining"].as<int>(), 0, 100));
}

}  // namespace

void PairingManager::parseAgentSnapshot(JsonDocument& document) {
  const uint32_t sequence = document["seq"] | 0U;
  if (agentOnline_ && sequence < agentSeq_) return;
  agentSeq_ = sequence;
  agentFocusId_ = document["focus_id"].as<String>();
  agentFocusSeq_ = document["focus_seq"] | 0U;
  agentQuota_.weeklyRemaining = quotaRemaining(document["quota"]["weekly"]);
  agentQuota_.fiveHourRemaining = quotaRemaining(document["quota"]["five_hour"]);

  agentCount_ = 0;
  for (JsonObjectConst item : document["items"].as<JsonArrayConst>()) {
    if (agentCount_ >= kMaxAgentSessions) break;
    AgentSession& agent = agents_[agentCount_++];
    agent.id = item["id"].as<String>();
    agent.title = item["title"] | "Codex session";
    agent.project = item["project"].as<String>();
    agent.activity = item["activity"] | "Session ready";
    agent.status = parseAgentStatus(item["status"]);
    agent.unread = item["unread"] | false;
  }
  agentOnline_ = true;
}

void PairingManager::requestAgentList() {
  StaticJsonDocument<256> request;
  request["t"] = "agent_list_req";
  request["limit"] = kMaxAgentSessions;
  sendDocument(request);
}

void PairingManager::disconnect(bool manual) {
  client_.stop();
  incoming_.clear();
  connectedName_.clear();
  state_ = LinkState::Offline;
  agentOnline_ = false;
  setAudioReady(false);
  manualDisconnect_ = manual;
  missedPongs_ = 0;
  if (manual) {
    targetId_.clear();
    targetToken_.clear();
    if (settings_) {
      settings_->lastMacId.clear();
      persistSettings();
    }
  }
}

void PairingManager::connectionLost() {
  client_.stop();
  connectedName_.clear();
  state_ = LinkState::Offline;
  agentOnline_ = false;
  setAudioReady(false);
  scheduleReconnect();
}

void PairingManager::scheduleReconnect() {
  nextConnectMs_ = millis() + reconnectDelayMs_;
  reconnectDelayMs_ = min<uint32_t>(reconnectDelayMs_ * 2, kReconnectMaxMs);
  discoveryRequested_ = true;
}

bool PairingManager::deletePairing(size_t index) {
  if (index >= pairedCount_) return false;
  if (pairedCurrent(index)) disconnect(true);
  for (size_t i = index; i + 1 < pairedCount_; ++i) paired_[i] = paired_[i + 1];
  --pairedCount_;
  return store_.savePairedMacs(paired_, pairedCount_);
}

bool PairingManager::audioEndpoint(IPAddress& ip, uint8_t token[32]) const {
  bool ready;
  uint8_t address[4];
  portENTER_CRITICAL(&audioMux_);
  ready = audioReady_;
  memcpy(address, audioIp_, sizeof(address));
  memcpy(token, audioToken_, 32);
  portEXIT_CRITICAL(&audioMux_);
  if (!ready) return false;
  ip = IPAddress(address[0], address[1], address[2], address[3]);
  return true;
}

int PairingManager::pairedIndexById(const String& id) const {
  for (size_t i = 0; i < pairedCount_; ++i) {
    if (paired_[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

int PairingManager::discoveredIndexById(const String& id) const {
  for (size_t i = 0; i < discoveredCount_; ++i) {
    if (discovered_[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

bool PairingManager::pairedOnline(size_t index) const {
  return index < pairedCount_ && discoveredIndexById(paired_[index].id) >= 0;
}

bool PairingManager::pairedCurrent(size_t index) const {
  return index < pairedCount_ && connected() && paired_[index].id == targetId_;
}

String PairingManager::statusText() const {
  switch (state_) {
    case LinkState::Offline: return "Mac offline";
    case LinkState::Discovering: return "Finding Mac...";
    case LinkState::Connecting: return "Connecting...";
    case LinkState::AwaitingPairCode: return "Enter pair code";
    case LinkState::Authenticating: return "Authenticating...";
    case LinkState::Connected: return String("Connected ") + connectedName_;
  }
  return "Mac offline";
}

void PairingManager::persistSettings() {
  if (settings_) store_.saveSettings(*settings_);
}

void PairingManager::setAudioReady(bool ready) {
  uint8_t decoded[32]{};
  if (ready) {
    if (targetToken_.length() != 64) ready = false;
    for (size_t i = 0; ready && i < 32; ++i) {
      const char highChar = targetToken_[i * 2];
      const char lowChar = targetToken_[i * 2 + 1];
      const int high = isDigit(highChar) ? highChar - '0' :
                       (tolower(highChar) >= 'a' && tolower(highChar) <= 'f'
                            ? tolower(highChar) - 'a' + 10 : -1);
      const int low = isDigit(lowChar) ? lowChar - '0' :
                      (tolower(lowChar) >= 'a' && tolower(lowChar) <= 'f'
                           ? tolower(lowChar) - 'a' + 10 : -1);
      if (high < 0 || low < 0) {
        ready = false;
      } else {
        decoded[i] = static_cast<uint8_t>((high << 4) | low);
      }
    }
  }
  portENTER_CRITICAL(&audioMux_);
  audioReady_ = ready;
  if (ready) {
    for (size_t i = 0; i < 4; ++i) audioIp_[i] = targetIp_[i];
    memcpy(audioToken_, decoded, sizeof(audioToken_));
  } else {
    memset(audioToken_, 0, sizeof(audioToken_));
  }
  portEXIT_CRITICAL(&audioMux_);
}

}  // namespace cardbridge
