#pragma once

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "app_config.h"
#include "models.h"
#include "settings_store.h"
#include "wifi_mgr.h"

class NimBLEServer;
class NimBLECharacteristic;

namespace cardbridge {

class PairingManager {
 public:
  PairingManager(SettingsStore& store, WifiManager& wifi)
      : store_(store), wifi_(wifi) {}

  void begin(DeviceSettings* settings);
  void tick();
  void requestDiscovery();
  void openBluetoothPairingWindow();
  bool bluetoothPairingOpen() const;
  uint32_t bluetoothPairingSecondsRemaining() const;
  bool connectToDiscovered(size_t index);
  bool connectToPaired(size_t index);
  void disconnect(bool manual = true);
  bool deletePairing(size_t index);
  bool submitPairCode(const String& sixDigits);

  bool sendKey(const char* key, const char* action, bool cmd, bool shift,
               bool option, bool control, uint32_t requestId = 0);
  uint32_t keyAckRevision() const { return keyAckRevision_; }
  uint32_t keyAckRequestId() const { return keyAckRequestId_; }
  bool keyAckOk() const { return keyAckOk_; }
  const String& keyAckError() const { return keyAckError_; }
  bool sendVoice(const char* action, bool locked, uint32_t requestId = 0,
                 bool sendEnter = false);
  uint32_t voiceAckRevision() const { return voiceAckRevision_; }
  uint32_t voiceAckRequestId() const { return voiceAckRequestId_; }
  bool voiceAckOk() const { return voiceAckOk_; }
  const String& voiceAckAction() const { return voiceAckAction_; }
  const String& voiceAckError() const { return voiceAckError_; }
  bool sendAgentAck(const String& sessionId);
  bool audioEndpoint(IPAddress& ip, uint8_t token[32]) const;
  bool audioStatus(uint32_t& received, uint32_t& updatedMs,
                   bool& outputReady) const;
  bool bluetoothMode() const {
    return settings_ && settings_->connectionMode == ConnectionMode::Bluetooth;
  }
  bool sendBleAudio(uint32_t sequence, uint32_t timestampMs,
                    const int16_t* samples, size_t count);

  // NimBLE callbacks forward only bounded byte/state changes here; protocol
  // parsing remains on the Arduino loop task.
  void bleConnected(uint16_t connHandle, const String& address,
                    uint8_t addressType, uint16_t mtu);
  void bleDisconnected(uint16_t connHandle, int reason);
  void bleMtuChanged(uint16_t connHandle, uint16_t mtu);
  void bleControlWritten(const uint8_t* data, size_t length);
  void bleControlSubscribed(uint16_t connHandle, bool enabled);

  LinkState state() const { return state_; }
  bool connected() const { return state_ == LinkState::Connected; }
  String connectedName() const { return connectedName_; }
  String statusText() const;
  bool pairCodeRequested() const { return state_ == LinkState::AwaitingPairCode; }
  const String& bridgeVersion() const { return bridgeVersion_; }
  uint32_t bridgeBuild() const { return bridgeBuild_; }
  uint8_t bridgeProtocolMajor() const { return bridgeProtocolMajor_; }
  uint8_t bridgeProtocolMinor() const { return bridgeProtocolMinor_; }
  const String& compatibilityReason() const { return compatibilityReason_; }
  const String& requiredFirmware() const { return requiredFirmware_; }

  size_t pairedCount() const { return pairedCount_; }
  const PairedMac& paired(size_t index) const { return paired_[index]; }
  bool pairedOnline(size_t index) const;
  bool pairedCurrent(size_t index) const;

  size_t discoveredCount() const { return discoveredCount_; }
  const DiscoveredMac& discovered(size_t index) const { return discovered_[index]; }

  bool agentOnline() const { return agentOnline_ && connected(); }
  size_t agentCount() const { return agentCount_; }
  const AgentSession& agent(size_t index) const { return agents_[index]; }
  const String& agentFocusId() const { return agentFocusId_; }
  uint32_t agentFocusSeq() const { return agentFocusSeq_; }
  const AgentQuota& agentQuota() const { return agentQuota_; }

 private:
  struct ConnectResult {
    uint32_t generation;
    bool connected;
  };

  struct ConnectTaskContext {
    PairingManager* manager;
    IPAddress ip;
    uint16_t port;
    uint32_t generation;
  };

  static void connectTaskEntry(void* argument);
  int pairedIndexById(const String& id) const;
  int discoveredIndexById(const String& id) const;
  bool startDiscovery(bool forReconnect);
  void pollDiscovery();
  void collectDiscoveryResults(mdns_result_t* results);
  void attemptConnection();
  void pollConnectionAttempt();
  void cancelConnectionAttempt();
  void sendHello();
  bool sendDocument(JsonDocument& document);
  void readIncoming();
  void handleLine(const String& line);
  void parseBridgeMetadata(JsonDocument& document);
  void parseAgentSnapshot(JsonDocument& document);
  void requestAgentList();
  void connectionLost();
  void scheduleReconnect();
  void persistSettings();
  void setAudioReady(bool ready);
  bool beginBluetooth();
  void tickBluetooth();
  bool sendBluetoothLine(const String& line);
  void readBluetoothIncoming();
  void startBluetoothAdvertising();
  void stopBluetooth();

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
  String bridgeVersion_;
  uint32_t bridgeBuild_ = 0;
  uint8_t bridgeProtocolMajor_ = 0;
  uint8_t bridgeProtocolMinor_ = 0;
  String compatibilityReason_;
  String requiredFirmware_;
  uint32_t keyAckRevision_ = 0;
  uint32_t keyAckRequestId_ = 0;
  bool keyAckOk_ = false;
  String keyAckError_;
  uint32_t voiceAckRevision_ = 0;
  uint32_t voiceAckRequestId_ = 0;
  bool voiceAckOk_ = false;
  String voiceAckAction_;
  String voiceAckError_;

  AgentSession agents_[kMaxAgentSessions];
  size_t agentCount_ = 0;
  String agentFocusId_;
  uint32_t agentFocusSeq_ = 0;
  AgentQuota agentQuota_;
  uint32_t agentSeq_ = 0;
  bool agentOnline_ = false;
  // The largest inbound message is an eight-session agent snapshot. Keeping
  // its JSON arena in the object avoids a 4 KiB loop-task stack spike and
  // repeated heap fragmentation on every status update.
  StaticJsonDocument<8192> incomingDocument_;

  LinkState state_ = LinkState::Offline;
  bool mdnsStarted_ = false;
  mdns_search_once_t* discoverySearch_ = nullptr;
  bool discoveryRequested_ = false;
  bool discoveryForReconnect_ = false;
  bool rediscoveryRequired_ = true;
  bool manualDisconnect_ = false;
  QueueHandle_t connectResultQueue_ = nullptr;
  bool connectInFlight_ = false;
  bool cancelConnect_ = false;
  uint32_t connectGeneration_ = 0;
  uint8_t missedPongs_ = 0;
  uint32_t lastHeartbeatMs_ = 0;
  uint32_t nextConnectMs_ = 0;
  uint32_t reconnectDelayMs_ = kReconnectMinMs;

  NimBLEServer* bleServer_ = nullptr;
  NimBLECharacteristic* bleControlTx_ = nullptr;
  NimBLECharacteristic* bleControlRx_ = nullptr;
  NimBLECharacteristic* bleAudioTx_ = nullptr;
  volatile bool bleConnected_ = false;
  volatile bool bleControlSubscribed_ = false;
  volatile bool bleHelloPending_ = false;
  uint16_t bleConnHandle_ = 0xFFFF;
  uint16_t bleMtu_ = 23;
  String blePeerAddress_;
  uint8_t blePeerAddressType_ = 0;
  uint32_t bleStreamId_ = 0;
  uint32_t blePairingDeadlineMs_ = 0;
  static constexpr size_t kBleRxCapacity = 8192;
  char bleRx_[kBleRxCapacity]{};
  size_t bleRxLength_ = 0;
  bool bleRxOverflow_ = false;
  mutable portMUX_TYPE bleMux_ = portMUX_INITIALIZER_UNLOCKED;

  // Only POD data crosses from the UI/control loop to the two audio tasks.
  // This avoids cross-core access to Arduino String internals.
  mutable portMUX_TYPE audioMux_ = portMUX_INITIALIZER_UNLOCKED;
  bool audioReady_ = false;
  uint8_t audioIp_[4]{};
  uint8_t audioToken_[32]{};
  bool audioStatusSeen_ = false;
  bool audioOutputReady_ = false;
  uint32_t audioReceived_ = 0;
  uint32_t audioStatusMs_ = 0;
};

}  // namespace cardbridge
