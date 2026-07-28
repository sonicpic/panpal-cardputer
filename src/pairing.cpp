#include "pairing.h"

#include <cstring>
#include <new>
#include <NimBLEDevice.h>
#include <lwip/def.h>
#include <mbedtls/md.h>

#include "adpcm.h"
#include "generated_version.h"

namespace cardbridge {
namespace {

constexpr uint32_t kDiscoveryTimeoutMs = 3000;
constexpr char kBleServiceUuid[] = "7e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kBleControlRxUuid[] = "7e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kBleControlTxUuid[] = "7e400003-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kBleAudioTxUuid[] = "7e400004-b5a3-f393-e0a9-e50e24dcca9e";

struct __attribute__((packed)) BleAudioBodyHeader {
  uint32_t streamId;
  uint32_t sequence;
  uint32_t timestampMs;
  int16_t predictor;
  uint8_t index;
  uint8_t reserved;
};

struct __attribute__((packed)) BleAudioFragmentHeader {
  uint8_t magic[2];
  uint32_t sequence;
  uint8_t index;
  uint8_t count;
};

constexpr size_t kBleAdpcmBytes = 160;
constexpr size_t kBleAudioHmacBytes = 8;
constexpr size_t kBleAudioBodyBytes =
    sizeof(BleAudioBodyHeader) + kBleAdpcmBytes + kBleAudioHmacBytes;

class CardBridgeBleServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit CardBridgeBleServerCallbacks(PairingManager* manager)
      : manager_(manager) {}

  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    // Let Windows finish pairing and GATT enumeration with the controller's
    // negotiated defaults. Forcing parameters inside this callback can race
    // WinRT service discovery and surface as GATT "Unreachable".
    manager_->bleConnected(info.getConnHandle(),
                           info.getAddress().toString().c_str(),
                           info.getAddress().getType(),
                           info.getMTU());
  }

  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
    manager_->bleDisconnected(info.getConnHandle(), reason);
    // advertiseOnDisconnect(true) owns the restart. Starting advertising a
    // second time from inside the NimBLE callback races the host state machine.
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo& info) override {
    manager_->bleMtuChanged(info.getConnHandle(), mtu);
  }

 private:
  PairingManager* manager_;
};

class CardBridgeBleCharacteristicCallbacks
    : public NimBLECharacteristicCallbacks {
 public:
  CardBridgeBleCharacteristicCallbacks(PairingManager* manager, bool receiver)
      : manager_(manager), receiver_(receiver) {}

  void onWrite(NimBLECharacteristic* characteristic,
               NimBLEConnInfo&) override {
    if (!receiver_) return;
    const std::string& value = characteristic->getValue();
    manager_->bleControlWritten(
        reinterpret_cast<const uint8_t*>(value.data()), value.size());
  }

  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info,
                   uint16_t value) override {
    if (receiver_) return;
    manager_->bleControlSubscribed(info.getConnHandle(), value != 0);
  }

 private:
  PairingManager* manager_;
  bool receiver_;
};

bool deadlineReached(uint32_t deadline) {
  return static_cast<int32_t>(millis() - deadline) >= 0;
}

String resultTxt(const mdns_result_t* result, const char* key) {
  if (!result || !key) return String();
  for (size_t i = 0; i < result->txt_count; ++i) {
    if (result->txt[i].key && strcmp(result->txt[i].key, key) == 0) {
      return String(result->txt[i].value ? result->txt[i].value : "");
    }
  }
  return String();
}

IPAddress resultIpv4(const mdns_result_t* result) {
  if (!result) return IPAddress();
  for (mdns_ip_addr_t* address = result->addr; address; address = address->next) {
    if (address->addr.type == MDNS_IP_PROTOCOL_V4) {
      return IPAddress(address->addr.u_addr.ip4.addr);
    }
  }
  return IPAddress();
}

}  // namespace

void PairingManager::begin(DeviceSettings* settings) {
  settings_ = settings;
  pairedCount_ = store_.loadPairedMacs(paired_, kMaxPairedMacs);
  char deviceId[13];
  const uint64_t efuse = ESP.getEfuseMac();
  snprintf(deviceId, sizeof(deviceId), "%012llx",
           static_cast<unsigned long long>(efuse & 0xFFFFFFFFFFFFULL));
  deviceId_ = deviceId;
  incoming_.reserve(4096);
  connectResultQueue_ = xQueueCreate(1, sizeof(ConnectResult));
  if (!connectResultQueue_) {
    Serial.println("[pairing] failed to create connection result queue");
  }
  if (settings_ && !settings_->lastMacId.isEmpty()) targetId_ = settings_->lastMacId;
  if (bluetoothMode() && !beginBluetooth()) {
    Serial.println("[ble] initialization failed");
  }
}

bool PairingManager::beginBluetooth() {
  const int pairedIndex = pairedIndexById(targetId_);
  targetToken_ = pairedIndex >= 0 ? paired_[pairedIndex].token : String();
  targetName_ = pairedIndex >= 0 ? paired_[pairedIndex].name : String("Computer");
  bleStreamId_ = esp_random();

  const String advertisedName =
      String("PanPal-") + deviceId_.substring(deviceId_.length() - 4);
  if (!NimBLEDevice::init(advertisedName.c_str())) return false;
  NimBLEDevice::setMTU(247);
  // Bond and encrypt the radio link without depending on a BLE address for
  // application identity. The existing six-digit code and 32-byte token still
  // authenticate CardBridge at the protocol layer.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  bleServer_ = NimBLEDevice::createServer();
  if (!bleServer_) return false;
  bleServer_->setCallbacks(new CardBridgeBleServerCallbacks(this));
  bleServer_->advertiseOnDisconnect(true);

  NimBLEService* service = bleServer_->createService(kBleServiceUuid);
  if (!service) return false;
  bleControlRx_ = service->createCharacteristic(
      kBleControlRxUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR, 512);
  bleControlTx_ = service->createCharacteristic(
      kBleControlTxUuid, NIMBLE_PROPERTY::NOTIFY, 512);
  bleAudioTx_ = service->createCharacteristic(
      kBleAudioTxUuid, NIMBLE_PROPERTY::NOTIFY, 512);
  if (!bleControlRx_ || !bleControlTx_ || !bleAudioTx_) return false;
  bleControlRx_->setCallbacks(
      new CardBridgeBleCharacteristicCallbacks(this, true));
  bleControlTx_->setCallbacks(
      new CardBridgeBleCharacteristicCallbacks(this, false));
  // NimBLE-Arduino 2.x starts the complete attribute database from the
  // server. NimBLEService::start() is a deprecated no-op, which leaves the
  // advertised service without our control/audio characteristics on Windows.
  if (!bleServer_->start()) return false;

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(advertisedName.c_str());
  advertising->addServiceUUID(kBleServiceUuid);
  advertising->enableScanResponse(true);
  if (!advertising->start()) return false;
  state_ = LinkState::Discovering;
  Serial.printf("[ble] advertising %s device=%s heap=%u\n",
                advertisedName.c_str(), deviceId_.c_str(), ESP.getFreeHeap());
  return true;
}

void PairingManager::stopBluetooth() {
  if (bleServer_ && bleConnHandle_ != 0xFFFF) {
    bleServer_->disconnect(bleConnHandle_);
  }
  NimBLEDevice::stopAdvertising();
  NimBLEDevice::deinit(true);
  bleServer_ = nullptr;
  bleControlRx_ = nullptr;
  bleControlTx_ = nullptr;
  bleAudioTx_ = nullptr;
}

void PairingManager::startBluetoothAdvertising() {
  if (!bluetoothMode() || bleConnected_) return;
  NimBLEDevice::startAdvertising();
  state_ = LinkState::Discovering;
}

void PairingManager::bleConnected(uint16_t connHandle, const String& address,
                                  uint8_t addressType, uint16_t mtu) {
  portENTER_CRITICAL(&bleMux_);
  bleConnected_ = true;
  bleControlSubscribed_ = false;
  bleHelloPending_ = false;
  bleConnHandle_ = connHandle;
  bleMtu_ = mtu;
  portEXIT_CRITICAL(&bleMux_);
  blePeerAddress_ = address;
  blePeerAddressType_ = addressType;
  state_ = LinkState::Connecting;
  Serial.printf("[ble] connected peer=%s mtu=%u\n", address.c_str(), mtu);
}

void PairingManager::bleDisconnected(uint16_t connHandle, int reason) {
  portENTER_CRITICAL(&bleMux_);
  if (bleConnHandle_ == connHandle) {
    bleConnected_ = false;
    bleControlSubscribed_ = false;
    bleHelloPending_ = false;
    bleConnHandle_ = 0xFFFF;
    bleRxLength_ = 0;
    bleRxOverflow_ = false;
  }
  portEXIT_CRITICAL(&bleMux_);
  agentOnline_ = false;
  setAudioReady(false);
  incoming_.clear();
  missedPongs_ = 0;
  state_ = LinkState::Discovering;
  Serial.printf(
      "[ble] disconnected reason=%d; advertising for Bluetooth reconnect\n",
      reason);
}

void PairingManager::bleMtuChanged(uint16_t connHandle, uint16_t mtu) {
  portENTER_CRITICAL(&bleMux_);
  if (bleConnHandle_ == connHandle) bleMtu_ = mtu;
  portEXIT_CRITICAL(&bleMux_);
  Serial.printf("[ble] mtu=%u\n", mtu);
}

void PairingManager::bleControlSubscribed(uint16_t connHandle, bool enabled) {
  portENTER_CRITICAL(&bleMux_);
  if (bleConnHandle_ == connHandle) {
    bleControlSubscribed_ = enabled;
    bleHelloPending_ = enabled;
  }
  portEXIT_CRITICAL(&bleMux_);
}

void PairingManager::bleControlWritten(const uint8_t* data, size_t length) {
  if (!data || !length) return;
  portENTER_CRITICAL(&bleMux_);
  for (size_t i = 0; i < length; ++i) {
    const char character = static_cast<char>(data[i]);
    if (bleRxOverflow_) {
      if (character == '\n') bleRxOverflow_ = false;
      continue;
    }
    if (bleRxLength_ >= kBleRxCapacity) {
      bleRxLength_ = 0;
      bleRxOverflow_ = true;
      continue;
    }
    bleRx_[bleRxLength_++] = character;
  }
  portEXIT_CRITICAL(&bleMux_);
}

void PairingManager::readBluetoothIncoming() {
  char chunk[256];
  for (;;) {
    size_t count = 0;
    portENTER_CRITICAL(&bleMux_);
    count = min<size_t>(sizeof(chunk), bleRxLength_);
    if (count) {
      memcpy(chunk, bleRx_, count);
      memmove(bleRx_, bleRx_ + count, bleRxLength_ - count);
      bleRxLength_ -= count;
    }
    portEXIT_CRITICAL(&bleMux_);
    if (!count) break;
    for (size_t i = 0; i < count; ++i) {
      const char character = chunk[i];
      if (character == '\n') {
        if (!incoming_.isEmpty()) handleLine(incoming_);
        incoming_.clear();
      } else if (character != '\r') {
        if (incoming_.length() < 4096) incoming_ += character;
        else incoming_.clear();
      }
    }
  }
}

bool PairingManager::sendBluetoothLine(const String& line) {
  uint16_t connHandle;
  uint16_t mtu;
  bool subscribed;
  portENTER_CRITICAL(&bleMux_);
  connHandle = bleConnHandle_;
  mtu = bleMtu_;
  subscribed = bleConnected_ && bleControlSubscribed_;
  portEXIT_CRITICAL(&bleMux_);
  if (!subscribed || !bleControlTx_ || connHandle == 0xFFFF) return false;
  const size_t chunkSize = max<size_t>(20, min<size_t>(180, mtu > 3 ? mtu - 3 : 20));
  for (size_t offset = 0; offset < line.length(); offset += chunkSize) {
    const size_t length = min(chunkSize, line.length() - offset);
    if (!bleControlTx_->notify(
            reinterpret_cast<const uint8_t*>(line.c_str()) + offset, length,
            connHandle)) {
      return false;
    }
    if (line.length() > chunkSize) delay(2);
  }
  return true;
}

void PairingManager::tickBluetooth() {
  readBluetoothIncoming();
  if (blePairingDeadlineMs_ && deadlineReached(blePairingDeadlineMs_)) {
    blePairingDeadlineMs_ = 0;
    Serial.println("[ble] new-computer pairing window closed");
    if (state_ == LinkState::AwaitingPairCode &&
        pairedIndexById(targetId_) < 0 && bleServer_ &&
        bleConnHandle_ != 0xFFFF) {
      bleServer_->disconnect(bleConnHandle_);
    }
  }
  bool helloPending = false;
  portENTER_CRITICAL(&bleMux_);
  helloPending = bleHelloPending_;
  bleHelloPending_ = false;
  portEXIT_CRITICAL(&bleMux_);
  if (helloPending) {
    state_ = LinkState::Connecting;
    sendHello();
    lastHeartbeatMs_ = millis();
    missedPongs_ = 0;
  }
  if (!bleConnected_) {
    agentOnline_ = false;
    setAudioReady(false);
    return;
  }
  if (state_ == LinkState::Connected) {
    const uint32_t now = millis();
    if (now - lastHeartbeatMs_ >= kHeartbeatMs) {
      StaticJsonDocument<256> ping;
      ping["t"] = "ping";
      const bool sent = sendDocument(ping);
      ++missedPongs_;
      if (!sent) {
        Serial.printf("[ble] heartbeat notify busy (miss=%u)\n", missedPongs_);
      }
      // A transient full notification queue is normal on BLE. Only tear down
      // the link after the same three-miss budget used for absent pongs.
      if (missedPongs_ >= kHeartbeatMissLimit) {
        if (bleServer_ && bleConnHandle_ != 0xFFFF) {
          bleServer_->disconnect(bleConnHandle_);
        }
        return;
      }
      lastHeartbeatMs_ = now;
    }
  }
}

bool PairingManager::sendBleAudio(uint32_t sequence, uint32_t timestampMs,
                                  const int16_t* samples, size_t count) {
  uint16_t connHandle;
  uint16_t mtu;
  uint8_t token[32];
  bool ready;
  portENTER_CRITICAL(&bleMux_);
  connHandle = bleConnHandle_;
  mtu = bleMtu_;
  const bool linkReady = bleConnected_ && bleControlSubscribed_;
  portEXIT_CRITICAL(&bleMux_);
  portENTER_CRITICAL(&audioMux_);
  ready = audioReady_;
  memcpy(token, audioToken_, sizeof(token));
  portEXIT_CRITICAL(&audioMux_);
  if (!linkReady || !ready || !bleAudioTx_ || connHandle == 0xFFFF) return false;

  uint8_t body[kBleAudioBodyBytes]{};
  auto* header = reinterpret_cast<BleAudioBodyHeader*>(body);
  header->streamId = htonl(bleStreamId_);
  header->sequence = htonl(sequence);
  header->timestampMs = htonl(timestampMs);
  ImaAdpcmState adpcmState;
  if (!encodeImaAdpcm(samples, count, body + sizeof(*header),
                      kBleAdpcmBytes, adpcmState)) {
    return false;
  }
  header->predictor = static_cast<int16_t>(
      htons(static_cast<uint16_t>(adpcmState.predictor)));
  header->index = adpcmState.index;

  uint8_t digest[32];
  mbedtls_md_context_t context;
  mbedtls_md_init(&context);
  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  const size_t authenticatedBytes = sizeof(*header) + kBleAdpcmBytes;
  const bool hmacOk = info && mbedtls_md_setup(&context, info, 1) == 0 &&
                      mbedtls_md_hmac_starts(&context, token, sizeof(token)) == 0 &&
                      mbedtls_md_hmac_update(&context, body,
                                             authenticatedBytes) == 0 &&
                      mbedtls_md_hmac_finish(&context, digest) == 0;
  mbedtls_md_free(&context);
  if (!hmacOk) return false;
  memcpy(body + authenticatedBytes, digest, kBleAudioHmacBytes);

  const size_t mtuPayload = mtu > 3 ? mtu - 3 : 20;
  if (mtuPayload <= sizeof(BleAudioFragmentHeader)) return false;
  const size_t fragmentPayload = mtuPayload - sizeof(BleAudioFragmentHeader);
  const uint8_t fragmentCount = static_cast<uint8_t>(
      (sizeof(body) + fragmentPayload - 1) / fragmentPayload);
  uint8_t packet[BLE_ATT_ATTR_MAX_LEN];
  for (uint8_t fragment = 0; fragment < fragmentCount; ++fragment) {
    const size_t offset = static_cast<size_t>(fragment) * fragmentPayload;
    const size_t payloadLength = min(fragmentPayload, sizeof(body) - offset);
    auto* fragmentHeader = reinterpret_cast<BleAudioFragmentHeader*>(packet);
    fragmentHeader->magic[0] = 'B';
    fragmentHeader->magic[1] = 'A';
    fragmentHeader->sequence = htonl(sequence);
    fragmentHeader->index = fragment;
    fragmentHeader->count = fragmentCount;
    memcpy(packet + sizeof(*fragmentHeader), body + offset, payloadLength);
    if (!bleAudioTx_->notify(packet, sizeof(*fragmentHeader) + payloadLength,
                             connHandle)) {
      return false;
    }
    if (fragmentCount > 1) delay(1);
  }
  return true;
}

void PairingManager::tick() {
  if (bluetoothMode()) {
    tickBluetooth();
    return;
  }
  // Both operations used to block this function (and therefore the keyboard/UI
  // loop) for 1.5-3 seconds. They are now polled without waiting.
  pollDiscovery();
  pollConnectionAttempt();

  if (!wifi_.connected()) {
    cancelConnectionAttempt();
    state_ = LinkState::Offline;
    agentOnline_ = false;
    setAudioReady(false);
    discoveredCount_ = 0;
    discoveryRequested_ = false;
    rediscoveryRequired_ = true;
    // An async mDNS search owns memory inside the mDNS service until its
    // timeout. Poll it to completion before stopping mDNS.
    if (mdnsStarted_ && !discoverySearch_) {
      MDNS.end();
      mdnsStarted_ = false;
      nextConnectMs_ = 0;
    }
    return;
  }

  if (!mdnsStarted_) {
    mdnsStarted_ = MDNS.begin((String("cardputer-") + deviceId_.substring(6)).c_str());
    if (mdnsStarted_) {
      discoveryRequested_ = true;
      rediscoveryRequired_ = true;
      nextConnectMs_ = 0;
    }
  }

  const bool autoReconnect = !manualDisconnect_ && !targetId_.isEmpty();
  if (discoveryRequested_ && !connectInFlight_) {
    startDiscovery(autoReconnect && deadlineReached(nextConnectMs_));
  }

  // NetworkClient::connect() runs on a worker while this task keeps servicing
  // input. No other code may touch client_ until the result is delivered.
  if (connectInFlight_) return;

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

  if (autoReconnect && !discoverySearch_ && deadlineReached(nextConnectMs_)) {
    int found = discoveredIndexById(targetId_);
    if (found >= 0 && !rediscoveryRequired_) {
      targetIp_ = discovered_[found].ip;
      targetPort_ = discovered_[found].port;
      targetName_ = discovered_[found].name;
      attemptConnection();
    } else {
      startDiscovery(true);
    }
  }
}

void PairingManager::requestDiscovery() {
  if (bluetoothMode()) {
    startBluetoothAdvertising();
    return;
  }
  if (wifi_.connected()) discoveryRequested_ = true;
}

bool PairingManager::startDiscovery(bool forReconnect) {
  if (!wifi_.connected() || !mdnsStarted_ || connectInFlight_) return false;
  if (discoverySearch_) {
    if (forReconnect) discoveryForReconnect_ = true;
    return true;
  }
  discoveryRequested_ = false;
  discoveryForReconnect_ = forReconnect;
  state_ = client_.connected() ? state_ : LinkState::Discovering;
  discoverySearch_ = mdns_query_async_new(
      nullptr, "_cardbridge", "_tcp", MDNS_TYPE_PTR, kDiscoveryTimeoutMs,
      kMaxDiscoveredMacs, nullptr);
  if (!discoverySearch_) {
    discoveryForReconnect_ = false;
    if (!client_.connected()) state_ = LinkState::Offline;
    if (forReconnect) scheduleReconnect();
    Serial.println("[pairing] failed to start async mDNS discovery");
    return false;
  }
  return true;
}

void PairingManager::pollDiscovery() {
  if (!discoverySearch_) return;
  mdns_result_t* results = nullptr;
  if (!mdns_query_async_get_results(discoverySearch_, 0, &results, nullptr)) {
    return;
  }

  mdns_search_once_t* finishedSearch = discoverySearch_;
  discoverySearch_ = nullptr;
  mdns_query_async_delete(finishedSearch);
  const bool forReconnect = discoveryForReconnect_;
  discoveryForReconnect_ = false;

  if (!wifi_.connected()) {
    discoveredCount_ = 0;
    rediscoveryRequired_ = true;
    if (results) mdns_query_results_free(results);
    return;
  }

  collectDiscoveryResults(results);
  if (results) mdns_query_results_free(results);
  if (!client_.connected()) state_ = LinkState::Offline;

  const int found = targetId_.isEmpty() ? -1 : discoveredIndexById(targetId_);
  rediscoveryRequired_ = !targetId_.isEmpty() && found < 0;
  if (!forReconnect || manualDisconnect_ || targetId_.isEmpty()) return;
  if (client_.connected() || connectInFlight_) return;
  if (found < 0) {
    scheduleReconnect();
    return;
  }
  targetIp_ = discovered_[found].ip;
  targetPort_ = discovered_[found].port;
  targetName_ = discovered_[found].name;
  attemptConnection();
}

void PairingManager::collectDiscoveryResults(mdns_result_t* results) {
  discoveredCount_ = 0;
  for (mdns_result_t* result = results;
       result && discoveredCount_ < kMaxDiscoveredMacs; result = result->next) {
    DiscoveredMac mac;
    mac.id = resultTxt(result, "id");
    mac.name = resultTxt(result, "name");
    if (mac.id.isEmpty() && result->hostname) mac.id = result->hostname;
    if (mac.name.isEmpty() && result->hostname) mac.name = result->hostname;
    mac.ip = resultIpv4(result);
    // The ESP32 mDNS resolver sometimes returns the service without its A
    // record (0.0.0.0). Keeping such an entry would wedge the reconnect
    // machine on an unconnectable address; drop it and let a later
    // discovery sweep resolve it properly.
    if (mac.ip == IPAddress()) continue;
    mac.port = result->port ? result->port : kControlPort;
    mac.paired = pairedIndexById(mac.id) >= 0;
    discovered_[discoveredCount_++] = mac;
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
  rediscoveryRequired_ = false;
  compatibilityReason_.clear();
  requiredFirmware_.clear();
  return true;
}

bool PairingManager::connectToPaired(size_t index) {
  if (index >= pairedCount_) return false;
  if (bluetoothMode()) {
    targetId_ = paired_[index].id;
    targetName_ = paired_[index].name;
    targetToken_ = paired_[index].token;
    manualDisconnect_ = false;
    startBluetoothAdvertising();
    return true;
  }
  const int discoveredIndex = discoveredIndexById(paired_[index].id);
  if (discoveredIndex < 0) return false;
  return connectToDiscovered(static_cast<size_t>(discoveredIndex));
}

void PairingManager::attemptConnection() {
  // A reconnect discovery can finish after a newer socket has already
  // connected. Never let that stale result stop the healthy TCP session.
  if (connectInFlight_ || client_.connected()) return;
  if (targetIp_ == IPAddress()) {
    // No usable address yet — back off and re-discover instead of silently
    // spinning against 0.0.0.0.
    scheduleReconnect();
    return;
  }
  if (!connectResultQueue_) {
    Serial.println("[pairing] connection unavailable: no result queue");
    scheduleReconnect();
    return;
  }
  state_ = LinkState::Connecting;
  client_.stop();  // Enforce exactly one active Mac connection.
  auto* context = new (std::nothrow) ConnectTaskContext{
      this, targetIp_, targetPort_, ++connectGeneration_};
  if (!context) {
    scheduleReconnect();
    state_ = LinkState::Offline;
    return;
  }
  cancelConnect_ = false;
  connectInFlight_ = true;
  if (xTaskCreatePinnedToCore(connectTaskEntry, "mac_connect", 4096, context, 1,
                              nullptr, 0) != pdPASS) {
    connectInFlight_ = false;
    delete context;
    scheduleReconnect();
    state_ = LinkState::Offline;
  }
}

void PairingManager::connectTaskEntry(void* argument) {
  auto* context = static_cast<ConnectTaskContext*>(argument);
  PairingManager* manager = context->manager;
  const ConnectResult result{
      context->generation,
      manager->client_.connect(context->ip, context->port, 1500) != 0,
  };
  xQueueOverwrite(manager->connectResultQueue_, &result);
  delete context;
  vTaskDelete(nullptr);
}

void PairingManager::pollConnectionAttempt() {
  if (!connectInFlight_ || !connectResultQueue_) return;
  ConnectResult result{};
  if (xQueueReceive(connectResultQueue_, &result, 0) != pdPASS) return;
  connectInFlight_ = false;
  const bool cancelled = cancelConnect_ || result.generation != connectGeneration_ ||
                         !wifi_.connected();
  cancelConnect_ = false;
  if (cancelled) {
    if (result.connected) client_.stop();
    state_ = LinkState::Offline;
    return;
  }
  if (!result.connected) {
    Serial.printf("[pairing] TCP connect failed target=%s %s:%u\n",
                  targetId_.c_str(), targetIp_.toString().c_str(), targetPort_);
    scheduleReconnect();
    state_ = LinkState::Offline;
    return;
  }
  // Any still-running mDNS query may refresh the computer list, but it no
  // longer owns reconnect once this TCP connection succeeds.
  discoveryForReconnect_ = false;
  Serial.printf("[pairing] TCP connected target=%s %s:%u\n",
                targetId_.c_str(), targetIp_.toString().c_str(), targetPort_);
  client_.setNoDelay(true);
  client_.setTimeout(1);
  incoming_.clear();
  missedPongs_ = 0;
  lastHeartbeatMs_ = millis();
  sendHello();
}

void PairingManager::cancelConnectionAttempt() {
  if (connectInFlight_) {
    cancelConnect_ = true;
  } else if (client_.connected()) {
    client_.stop();
  }
}

void PairingManager::sendHello() {
  const int index = pairedIndexById(targetId_);
  targetToken_ = index >= 0 ? paired_[index].token : String();
  // The authenticated hello includes a 64-character token plus every current
  // capability. It no longer fits the original 512-byte wire buffer.
  StaticJsonDocument<1280> hello;
  hello["t"] = "hello";
  hello["dev_id"] = deviceId_;
  if (targetToken_.isEmpty()) {
    hello["token"] = nullptr;
  } else {
    hello["token"] = targetToken_;
  }
  JsonObject device = hello.createNestedObject("device");
  device["model"] = "cardputer-adv";
  device["firmware"] = kFirmwareVersion;
  device["build"] = kFirmwareBuild;
  JsonObject protocol = hello.createNestedObject("protocol");
  protocol["major"] = kDeviceProtocolMajor;
  protocol["minor"] = kDeviceProtocolMinor;
  JsonArray capabilities = hello.createNestedArray("capabilities");
  for (size_t i = 0; i < kDeviceCapabilityCount; ++i) {
    capabilities.add(kDeviceCapabilities[i]);
  }
  if (!sendDocument(hello)) {
    Serial.printf("[pairing] hello send failed target=%s\n", targetId_.c_str());
    connectionLost();
  } else {
    Serial.printf("[pairing] hello sent target=%s auth=%s\n", targetId_.c_str(),
                  targetToken_.isEmpty() ? "pair-code" : "saved-token");
    state_ = targetToken_.isEmpty() ? LinkState::AwaitingPairCode
                                    : LinkState::Authenticating;
  }
}

bool PairingManager::submitPairCode(const String& sixDigits) {
  const bool linkConnected = bluetoothMode()
                                 ? bleConnected_ && bleControlSubscribed_
                                 : client_.connected();
  if (!linkConnected || sixDigits.length() != 6) return false;
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
                             bool shift, bool option, bool control,
                             uint32_t requestId) {
  if (!connected() ||
      (bluetoothMode() ? !bleConnected_ : !client_.connected())) return false;
  StaticJsonDocument<384> document;
  document["t"] = "key";
  document["k"] = key;
  document["a"] = action;
  document["request_id"] = requestId;
  JsonArray modifiers = document.createNestedArray("m");
  if (cmd) modifiers.add("cmd");
  if (shift) modifiers.add("shift");
  if (option) modifiers.add("alt");
  if (control) modifiers.add("ctrl");
  return sendDocument(document);
}

void PairingManager::openBluetoothPairingWindow() {
  if (!bluetoothMode()) return;
  blePairingDeadlineMs_ = millis() + 60000UL;
  startBluetoothAdvertising();
  Serial.println("[ble] new-computer pairing open for 60 seconds");
}

bool PairingManager::bluetoothPairingOpen() const {
  return bluetoothMode() && blePairingDeadlineMs_ &&
         !deadlineReached(blePairingDeadlineMs_);
}

uint32_t PairingManager::bluetoothPairingSecondsRemaining() const {
  if (!bluetoothPairingOpen()) return 0;
  return (blePairingDeadlineMs_ - millis() + 999UL) / 1000UL;
}

bool PairingManager::sendVoice(const char* action, bool locked,
                               uint32_t requestId, bool sendEnter) {
  StaticJsonDocument<192> document;
  document["t"] = "voice";
  document["a"] = action;
    document["locked"] = locked;
    document["request_id"] = requestId;
    if (strcmp(action, "up") == 0) document["send_enter"] = sendEnter;
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
  if (bluetoothMode()) {
    if (!bleConnected_ || !bleControlSubscribed_) return false;
  } else if (!client_.connected()) {
    return false;
  }
  if (state_ == LinkState::Connected && targetToken_.length() == 64 &&
      !document["token"].is<String>()) {
    document["token"] = targetToken_;
  }
  if (document.overflowed()) return false;
  char line[1024];
  const size_t jsonLength = measureJson(document);
  if (jsonLength + 1 > sizeof(line)) {
    Serial.printf("[pairing] control message too large: %u bytes\n",
                  static_cast<unsigned>(jsonLength));
    return false;
  }
  const size_t written = serializeJson(document, line, sizeof(line));
  if (bluetoothMode()) {
    String payload;
    payload.reserve(written + 1);
    payload.concat(line, written);
    payload += '\n';
    return sendBluetoothLine(payload);
  }
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
  if (bluetoothMode() && incomingDocument_["mac_id"].is<const char*>()) {
    targetId_ = incomingDocument_["mac_id"].as<String>();
  }
  if (state_ == LinkState::Connected &&
      (type == "ping" || type == "pong" || type == "agent_status" ||
       type == "agent_list" || type == "key_ack" || type == "voice_ack") &&
      incomingDocument_["token"].as<String>() != targetToken_) {
    return;
  }
  if (type == "key_ack") {
    keyAckRequestId_ = incomingDocument_["request_id"] | 0U;
    keyAckOk_ = incomingDocument_["ok"] | false;
    keyAckError_ = incomingDocument_["error"] | "";
    ++keyAckRevision_;
    return;
  }
  if (type == "voice_ack") {
    voiceAckRequestId_ = incomingDocument_["request_id"] | 0U;
    voiceAckAction_ = incomingDocument_["a"] | "";
    voiceAckOk_ = incomingDocument_["ok"] | false;
    voiceAckError_ = incomingDocument_["error"] | "";
    ++voiceAckRevision_;
    return;
  }
  if (type == "pong") {
    missedPongs_ = 0;
    if (incomingDocument_["audio_received"].is<uint32_t>()) {
      const uint32_t received = incomingDocument_["audio_received"].as<uint32_t>();
      const bool outputReady = incomingDocument_["audio_output_ready"] | false;
      portENTER_CRITICAL(&audioMux_);
      audioStatusSeen_ = true;
      audioOutputReady_ = outputReady;
      audioReceived_ = received;
      audioStatusMs_ = millis();
      portEXIT_CRITICAL(&audioMux_);
    }
    return;
  }
  if (type == "ping") {
    StaticJsonDocument<256> pong;
    pong["t"] = "pong";
    sendDocument(pong);
    return;
  }
  if (type == "upgrade_required") {
    compatibilityReason_ = incomingDocument_["reason"] | "version_mismatch";
    requiredFirmware_ = incomingDocument_["required"]["min_firmware"] | "";
    parseBridgeMetadata(incomingDocument_);
    if (bluetoothMode()) {
      if (bleServer_ && bleConnHandle_ != 0xFFFF) {
        bleServer_->disconnect(bleConnHandle_);
      }
    } else {
      client_.stop();
    }
    incoming_.clear();
    connectedName_ = targetName_;
    state_ = LinkState::Incompatible;
    agentOnline_ = false;
    setAudioReady(false);
    manualDisconnect_ = true;
    missedPongs_ = 0;
    return;
  }
  if (type == "pair_required") {
    if (bluetoothMode() && pairedIndexById(targetId_) < 0 &&
        !bluetoothPairingOpen()) {
      Serial.println("[ble] rejected unknown computer outside pairing window");
      if (bleServer_ && bleConnHandle_ != 0xFFFF) {
        bleServer_->disconnect(bleConnHandle_);
      }
      return;
    }
    parseBridgeMetadata(incomingDocument_);
    targetName_ = incomingDocument_["mac_name"] | targetName_;
    connectedName_ = targetName_;
    state_ = LinkState::AwaitingPairCode;
    Serial.printf("[pairing] pair code required target=%s\n", targetId_.c_str());
    return;
  }
  if (type == "pair_error") {
    state_ = LinkState::AwaitingPairCode;
    return;
  }
  if (type == "paired") {
    parseBridgeMetadata(incomingDocument_);
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
    paired_[index].transport = bluetoothMode()
                                   ? ConnectionMode::Bluetooth
                                   : ConnectionMode::Wifi;
    if (bluetoothMode()) {
      paired_[index].bleAddress = blePeerAddress_;
      paired_[index].bleAddressType = blePeerAddressType_;
    }
    const bool pairingSaved = store_.savePairedMacs(paired_, pairedCount_);
    if (!pairingSaved) {
      Serial.println("[pairing] ERROR: paired computer was not saved");
    }
    connectedName_ = paired_[index].name;
    state_ = LinkState::Connected;
    setAudioReady(true);
    reconnectDelayMs_ = kReconnectMinMs;
    requestAgentList();
    if (settings_) {
      settings_->lastMacId = targetId_;
      persistSettings();
    }
    Serial.printf("[pairing] paired target=%s saved=%s\n", targetId_.c_str(),
                  pairingSaved ? "yes" : "NO");
    return;
  }
  if (type == "hello_ok") {
    parseBridgeMetadata(incomingDocument_);
    connectedName_ = incomingDocument_["mac_name"] | targetName_;
    state_ = LinkState::Connected;
    blePairingDeadlineMs_ = 0;
    const int index = pairedIndexById(targetId_);
    if (index >= 0 && bluetoothMode()) {
      paired_[index].transport = ConnectionMode::Bluetooth;
      paired_[index].bleAddress = blePeerAddress_;
      paired_[index].bleAddressType = blePeerAddressType_;
      if (!store_.savePairedMacs(paired_, pairedCount_)) {
        Serial.println("[pairing] ERROR: Bluetooth bond metadata was not saved");
      }
    }
    setAudioReady(true);
    reconnectDelayMs_ = kReconnectMinMs;
    requestAgentList();
    if (settings_) {
      settings_->lastMacId = targetId_;
      persistSettings();
    }
    Serial.printf("[pairing] authenticated target=%s with saved token\n",
                  targetId_.c_str());
    return;
  }
  if (type == "auth_error") {
    Serial.printf("[pairing] saved token rejected target=%s; pairing required\n",
                  targetId_.c_str());
    const int index = pairedIndexById(targetId_);
    if (index >= 0) {
      paired_[index].token.clear();
      if (!store_.savePairedMacs(paired_, pairedCount_)) {
        Serial.println("[pairing] ERROR: invalid token state was not saved");
      }
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

void PairingManager::parseBridgeMetadata(JsonDocument& document) {
  JsonVariantConst app = document["app"];
  bridgeVersion_ = app["version"] | "";
  bridgeBuild_ = app["build"] | 0U;
  JsonVariantConst protocol = document["protocol"];
  if (protocol.isNull()) {
    // A bridge without explicit protocol metadata is the shipped legacy v1.
    bridgeProtocolMajor_ = 1;
    bridgeProtocolMinor_ = 0;
  } else {
    bridgeProtocolMajor_ = protocol["major"] | 0;
    bridgeProtocolMinor_ = protocol["minor"] | 0;
  }
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

AgentPhase parseAgentPhase(const char* value) {
  if (!value) return AgentPhase::None;
  if (!strcmp(value, "thinking")) return AgentPhase::Thinking;
  if (!strcmp(value, "tool")) return AgentPhase::Tool;
  return AgentPhase::None;
}

AgentQuotaMode parseAgentQuotaMode(const char* value) {
  if (!value) return AgentQuotaMode::Unknown;
  if (!strcmp(value, "subscription")) return AgentQuotaMode::Subscription;
  if (!strcmp(value, "api")) return AgentQuotaMode::Api;
  return AgentQuotaMode::Unknown;
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
  JsonVariantConst quota = document["quota"];
  // ArduinoJson 6 treats `variant | nullptr` as a null default even when the
  // variant contains a string. Use the typed conversion so api/subscription
  // modes survive the wire format instead of always degrading to Unknown.
  const char* quotaMode = quota["mode"].as<const char*>();
  if (quotaMode) {
    agentQuota_.mode = parseAgentQuotaMode(quotaMode);
  } else {
    // Older bridges expose only a subscription-availability boolean. They
    // cannot distinguish API from a failed lookup, so false remains Unknown.
    const bool available = quota["available"].is<bool>()
                               ? quota["available"].as<bool>()
                               : (!quota["weekly"].isNull() ||
                                  !quota["five_hour"].isNull());
    agentQuota_.mode = available ? AgentQuotaMode::Subscription
                                 : AgentQuotaMode::Unknown;
  }
  const bool subscription = agentQuota_.mode == AgentQuotaMode::Subscription;
  agentQuota_.weeklyRemaining = subscription
                                    ? quotaRemaining(quota["weekly"])
                                    : -1;
  agentQuota_.fiveHourRemaining = subscription
                                      ? quotaRemaining(quota["five_hour"])
                                      : -1;

  agentCount_ = 0;
  for (JsonObjectConst item : document["items"].as<JsonArrayConst>()) {
    if (agentCount_ >= kMaxAgentSessions) break;
    AgentSession& agent = agents_[agentCount_++];
    agent.id = item["id"].as<String>();
    agent.title = item["title"] | "Session";
    agent.project = item["project"].as<String>();
    agent.activity = item["activity"] | "Session ready";
    agent.status = parseAgentStatus(item["status"]);
    agent.phase = parseAgentPhase(item["phase"]);
    // Older CardBridge services did not send a phase. Treat an unqualified
    // Running snapshot as thinking instead of falsely showing a live command.
    if (agent.status == AgentStatus::Running && agent.phase == AgentPhase::None) {
      agent.phase = AgentPhase::Thinking;
    }
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
  if (bluetoothMode()) {
    if (bleServer_ && bleConnHandle_ != 0xFFFF) {
      bleServer_->disconnect(bleConnHandle_);
    }
  } else {
    cancelConnectionAttempt();
  }
  incoming_.clear();
  connectedName_.clear();
  state_ = LinkState::Offline;
  agentOnline_ = false;
  setAudioReady(false);
  manualDisconnect_ = manual;
  missedPongs_ = 0;
  compatibilityReason_.clear();
  requiredFirmware_.clear();
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
  if (bluetoothMode()) {
    connectedName_.clear();
    state_ = LinkState::Discovering;
    agentOnline_ = false;
    setAudioReady(false);
    if (bleServer_ && bleConnHandle_ != 0xFFFF) {
      bleServer_->disconnect(bleConnHandle_);
    }
    return;
  }
  cancelConnectionAttempt();
  Serial.printf("[pairing] TCP connection lost target=%s; reconnect scheduled\n",
                targetId_.c_str());
  connectedName_.clear();
  state_ = LinkState::Offline;
  agentOnline_ = false;
  setAudioReady(false);
  scheduleReconnect();
}

void PairingManager::scheduleReconnect() {
  nextConnectMs_ = millis() + reconnectDelayMs_;
  reconnectDelayMs_ = min<uint32_t>(reconnectDelayMs_ * 2, kReconnectMaxMs);
  rediscoveryRequired_ = true;
}

bool PairingManager::deletePairing(size_t index) {
  if (index >= pairedCount_) return false;
  if (pairedCurrent(index)) disconnect(true);
  if (bluetoothMode() && !paired_[index].bleAddress.isEmpty()) {
    NimBLEDevice::deleteBond(NimBLEAddress(
        paired_[index].bleAddress.c_str(), paired_[index].bleAddressType));
  }
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

bool PairingManager::audioStatus(uint32_t& received, uint32_t& updatedMs,
                                 bool& outputReady) const {
  bool seen;
  portENTER_CRITICAL(&audioMux_);
  seen = audioStatusSeen_;
  received = audioReceived_;
  updatedMs = audioStatusMs_;
  outputReady = audioOutputReady_;
  portEXIT_CRITICAL(&audioMux_);
  return seen;
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
  if (bluetoothMode()) return pairedCurrent(index);
  return index < pairedCount_ && discoveredIndexById(paired_[index].id) >= 0;
}

bool PairingManager::pairedCurrent(size_t index) const {
  return index < pairedCount_ && connected() && paired_[index].id == targetId_;
}

String PairingManager::statusText() const {
  switch (state_) {
    case LinkState::Offline:
      return bluetoothMode() ? "Bluetooth disconnected" : "Computer offline";
    case LinkState::Discovering:
      return bluetoothMode() ? "Bluetooth advertising" : "Finding computer...";
    case LinkState::Connecting: return "Connecting...";
    case LinkState::AwaitingPairCode: return "Enter pair code";
    case LinkState::Authenticating: return "Authenticating...";
    case LinkState::Connected: return String("Connected ") + connectedName_;
    case LinkState::Incompatible: return "Update required";
  }
  return bluetoothMode() ? "Bluetooth disconnected" : "Computer offline";
}

void PairingManager::persistSettings() {
  if (settings_ && !store_.saveSettings(*settings_)) {
    Serial.println("[nvs] ERROR: device settings were not saved");
  }
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
  audioStatusSeen_ = false;
  audioOutputReady_ = false;
  audioReceived_ = 0;
  audioStatusMs_ = 0;
  if (ready) {
    for (size_t i = 0; i < 4; ++i) audioIp_[i] = targetIp_[i];
    memcpy(audioToken_, decoded, sizeof(audioToken_));
  } else {
    memset(audioToken_, 0, sizeof(audioToken_));
  }
  portEXIT_CRITICAL(&audioMux_);
}

}  // namespace cardbridge
