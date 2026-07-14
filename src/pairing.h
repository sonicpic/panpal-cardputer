#pragma once

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>

#include "app_config.h"
#include "models.h"
#include "settings_store.h"
#include "wifi_mgr.h"

namespace cardbridge {

class PairingManager {
 public:
  PairingManager(SettingsStore& store, WifiManager& wifi)
      : store_(store), wifi_(wifi) {}

  void begin(DeviceSettings* settings);
  void tick();
  void requestDiscovery();
  bool connectToDiscovered(size_t index);
  bool connectToPaired(size_t index);
  void disconnect(bool manual = true);
  bool deletePairing(size_t index);
  bool submitPairCode(const String& sixDigits);

  bool sendKey(const char* key, const char* action, bool cmd, bool shift,
               bool option, bool control);
  bool audioEndpoint(IPAddress& ip, uint8_t token[32]) const;

  LinkState state() const { return state_; }
  bool connected() const { return state_ == LinkState::Connected; }
  String connectedName() const { return connectedName_; }
  String statusText() const;
  bool pairCodeRequested() const { return state_ == LinkState::AwaitingPairCode; }

  size_t pairedCount() const { return pairedCount_; }
  const PairedMac& paired(size_t index) const { return paired_[index]; }
  bool pairedOnline(size_t index) const;
  bool pairedCurrent(size_t index) const;

  size_t discoveredCount() const { return discoveredCount_; }
  const DiscoveredMac& discovered(size_t index) const { return discovered_[index]; }

 private:
  int pairedIndexById(const String& id) const;
  int discoveredIndexById(const String& id) const;
  void discover();
  void attemptConnection();
  void sendHello();
  bool sendDocument(JsonDocument& document);
  void readIncoming();
  void handleLine(const String& line);
  void connectionLost();
  void scheduleReconnect();
  void persistSettings();
  void setAudioReady(bool ready);

  SettingsStore& store_;
  WifiManager& wifi_;
  DeviceSettings* settings_ = nullptr;
  PairedMac paired_[kMaxPairedMacs];
  DiscoveredMac discovered_[kMaxDiscoveredMacs];
  size_t pairedCount_ = 0;
  size_t discoveredCount_ = 0;

  WiFiClient client_;
  String deviceId_;
  String incoming_;
  String targetId_;
  String targetName_;
  String targetToken_;
  IPAddress targetIp_;
  uint16_t targetPort_ = kControlPort;
  String connectedName_;

  LinkState state_ = LinkState::Offline;
  bool mdnsStarted_ = false;
  bool discoveryRequested_ = false;
  bool manualDisconnect_ = false;
  uint8_t missedPongs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  uint32_t nextConnectMs_ = 0;
  uint32_t reconnectDelayMs_ = kReconnectMinMs;

  // Only POD data crosses from the UI/control loop to the two audio tasks.
  // This avoids cross-core access to Arduino String internals.
  mutable portMUX_TYPE audioMux_ = portMUX_INITIALIZER_UNLOCKED;
  bool audioReady_ = false;
  uint8_t audioIp_[4]{};
  uint8_t audioToken_[32]{};
};

}  // namespace cardbridge
