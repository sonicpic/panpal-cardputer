#include "wifi_mgr.h"

#include <algorithm>

#include <esp_eap_client.h>
#include <esp_wifi.h>

namespace {

constexpr uint32_t kPersonalConnectTimeoutMs = 15000;
constexpr uint32_t kEnterpriseConnectTimeoutMs = 20000;
constexpr uint32_t kSavedNetworkSwitchDelayMs = 1500;
constexpr uint32_t kReconnectIntervalMs = 10000;

bool isEnterpriseAuth(wifi_auth_mode_t mode) {
  return mode == WIFI_AUTH_WPA2_ENTERPRISE;
}

}  // namespace

namespace cardbridge {

void WifiManager::begin() {
  savedCount_ = store_.loadWifiNetworks(saved_, kMaxWifiNetworks);
  scanResultQueue_ = xQueueCreate(1, sizeof(int16_t));
  if (!scanResultQueue_) Serial.println("[wifi] failed to create scan result queue");

  // Arduino-ESP32 2.x and 3.x persist different private Wi-Fi driver state in
  // the shared NVS partition. Reset that driver-owned state once after the
  // framework migration. Our network profiles and pairing records remain in
  // the separate "cardbridge" Preferences namespace.
  const bool driverMigrationPending = !store_.wifiDriverMigrationComplete();
  if (!driverMigrationPending) {
    WiFi.persistent(false);
  }
  WiFi.mode(WIFI_STA);
  if (driverMigrationPending) {
    const esp_err_t restored = esp_wifi_restore();
    if (restored == ESP_OK) {
      if (store_.markWifiDriverMigrationComplete()) {
        Serial.println("[wifi] driver state migration complete");
      } else {
        Serial.println(
            "[wifi] driver state restored; migration marker write failed");
      }
    } else {
      Serial.printf("[wifi] driver state migration failed: %s\n",
                    esp_err_to_name(restored));
    }
  }

  // esp_wifi_restore() resets the stored mode along with the stored station
  // configuration. Reapply STA mode, then keep subsequent credentials in RAM;
  // the application-owned multi-network list remains authoritative.
  WiFi.persistent(false);
  const esp_err_t volatileStorage = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (volatileStorage != ESP_OK) {
    Serial.printf("[wifi] volatile driver storage setup failed: %s\n",
                  esp_err_to_name(volatileStorage));
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(
      [this](arduino_event_id_t event, arduino_event_info_t info) {
        onWifiEvent(event, info);
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if (savedCount_ > 0) {
    tryKnownNetworks();
  } else {
    needsSetup_ = true;
    startScan();
  }
}

void WifiManager::tick() {
  pollScanResult();
  if (pendingScan_ && !scanning_) runPendingScan();

  // The boot-time scan can run before the STA interface is fully up and come
  // back empty. Keep retrying while we have never seen a network.
  if (!pendingScan_ && !scanEverSucceeded_ && !connected() &&
      connectStartedMs_ == 0 &&
      millis() - lastScanStartMs_ >= 4000) {
    startScan();
  }

  if (connected()) {
    connectStartedMs_ = 0;
    return;
  }

  // The radio scan runs on a low-priority worker. Do not start or tear down a
  // station connection underneath it; the main loop remains free to scan keys
  // and draw the UI while the worker waits for the WiFi driver.
  if (scanning_) return;

  const uint32_t now = millis();
  const uint8_t disconnectReason = lastDisconnectReason_;
  lastDisconnectReason_ = 0;
  if (disconnectReason == WIFI_REASON_802_1X_AUTH_FAILED &&
      reconnectIndex_ < savedCount_ &&
      saved_[reconnectIndex_].security == WifiSecurity::EnterprisePeap) {
    // This is a conclusive PEAP/MSCHAPv2 rejection, not a transient radio
    // failure. Do not repeatedly submit the same credential pair or leave
    // the driver in CONNECTING; return the user to the Wi-Fi menu instead.
    connectStartedMs_ = 0;
    lastReconnectMs_ = now;
    reconnectAttempts_ = savedCount_;
    enterpriseAuthRejected_ = true;
    enterpriseAuthRejectedSsid_ = saved_[reconnectIndex_].ssid;
    WiFi.setAutoReconnect(false);
    needsSetup_ = true;
    Serial.println(
        "[wifi] enterprise authentication rejected; edit credentials and retry");
    return;
  }
  const bool connectingEnterprise =
      reconnectIndex_ < savedCount_ &&
      saved_[reconnectIndex_].security == WifiSecurity::EnterprisePeap;
  const uint32_t connectTimeoutMs = connectingEnterprise
      ? kEnterpriseConnectTimeoutMs : kPersonalConnectTimeoutMs;
  if (connectStartedMs_ && now - connectStartedMs_ > connectTimeoutMs) {
    connectStartedMs_ = 0;
    lastReconnectMs_ = now;
    ++reconnectAttempts_;
    Serial.printf("[wifi] %s connection timed out after %lu ms (status=%d)\n",
                  connectingEnterprise ? "enterprise" : "personal",
                  static_cast<unsigned long>(connectTimeoutMs),
                  static_cast<int>(WiFi.status()));
    if (connectingEnterprise) {
      // The framework may report the 802.1X rejection a little after the
      // timeout. Stop here rather than forcing a second PEAP begin while the
      // EAP state machine is still winding down.
      WiFi.setAutoReconnect(false);
      enterpriseAuthRejected_ = true;
      enterpriseAuthRejectedSsid_ = saved_[reconnectIndex_].ssid;
      needsSetup_ = true;
      Serial.println(
          "[wifi] enterprise connection failed; edit credentials and retry");
    } else if (reconnectAttempts_ < savedCount_) {
      reconnectIndex_ = (reconnectIndex_ + 1) % savedCount_;
    } else {
      reconnectIndex_ = 0;
      needsSetup_ = true;
      if (!scanning_) startScan();
    }
  } else if (!connectStartedMs_ && reconnectAttempts_ > 0 &&
             reconnectAttempts_ < savedCount_ &&
             now - lastReconnectMs_ >= kSavedNetworkSwitchDelayMs &&
             !scanning_) {
    // Give the Wi-Fi driver a moment to leave CONNECTING before trying the
    // next saved profile. This matters especially after EAP authentication.
    startConnection(reconnectIndex_);
  } else if (!connectStartedMs_ && savedCount_ > 0 &&
             now - lastReconnectMs_ > kReconnectIntervalMs && !scanning_) {
    lastReconnectMs_ = now;
    tryKnownNetworks();
  }
}

// The Arduino async scan API is unreliable on this core (scanComplete() reports
// "done, 0 networks" instantly, sometimes -2). Keep the reliable synchronous
// driver call, but run it on core 0 at low priority so its 2-4 second wait never
// stalls keyboard sampling or rendering on the Arduino loop task.
void WifiManager::startScan() {
  if (pendingScan_ || scanning_) return;
  scanCount_ = 0;
  pendingScan_ = true;
}

void WifiManager::runPendingScan() {
  pendingScan_ = false;
  if (!scanResultQueue_) {
    Serial.println("[wifi] scan unavailable: no result queue");
    return;
  }
  scanning_ = true;
  WiFi.scanDelete();
  lastScanStartMs_ = millis();
  if (xTaskCreatePinnedToCore(scanTaskEntry, "wifi_scan", 4096, this, 1,
                              nullptr, 0) != pdPASS) {
    scanning_ = false;
    Serial.println("[wifi] failed to start scan worker");
  }
}

void WifiManager::scanTaskEntry(void* argument) {
  auto* manager = static_cast<WifiManager*>(argument);
  const int16_t result = WiFi.scanNetworks(false, false);
  xQueueOverwrite(manager->scanResultQueue_, &result);
  vTaskDelete(nullptr);
}

void WifiManager::pollScanResult() {
  if (!scanning_ || !scanResultQueue_) return;
  int16_t result = WIFI_SCAN_FAILED;
  if (xQueueReceive(scanResultQueue_, &result, 0) != pdPASS) return;
  scanning_ = false;
  Serial.printf("[wifi] worker scan result: %d\n", result);
  collectResults(result);
}

void WifiManager::collectResults(int16_t result) {
  if (result > 0) scanEverSucceeded_ = true;

  const size_t capacity = sizeof(scan_) / sizeof(scan_[0]);
  scanCount_ = 0;
  for (int i = 0; i < max<int16_t>(result, 0) && scanCount_ < capacity; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool duplicate = false;
    for (size_t j = 0; j < scanCount_; ++j) {
      if (scan_[j].ssid == ssid) {
        if (WiFi.RSSI(i) > scan_[j].rssi) scan_[j].rssi = WiFi.RSSI(i);
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;
    scan_[scanCount_].ssid = ssid;
    scan_[scanCount_].rssi = WiFi.RSSI(i);
    const wifi_auth_mode_t auth = WiFi.encryptionType(i);
    scan_[scanCount_].encrypted = auth != WIFI_AUTH_OPEN;
    scan_[scanCount_].enterprise = isEnterpriseAuth(auth);
    scan_[scanCount_].saved = isSaved(ssid);
    ++scanCount_;
  }

  // Saved networks remain manageable even when they are currently offline or
  // outside scan range. This also makes the current connection explicit.
  for (size_t i = 0; i < savedCount_ && scanCount_ < capacity; ++i) {
    bool present = false;
    for (size_t j = 0; j < scanCount_; ++j) {
      if (scan_[j].ssid == saved_[i].ssid) {
        present = true;
        break;
      }
    }
    if (present) continue;
    scan_[scanCount_].ssid = saved_[i].ssid;
    scan_[scanCount_].rssi = connected() && WiFi.SSID() == saved_[i].ssid
                                ? WiFi.RSSI() : -127;
    scan_[scanCount_].encrypted = saved_[i].security != WifiSecurity::Open;
    scan_[scanCount_].enterprise =
        saved_[i].security == WifiSecurity::EnterprisePeap;
    scan_[scanCount_].saved = true;
    ++scanCount_;
  }
  std::sort(scan_, scan_ + scanCount_, [](const WifiScanResult& a,
                                          const WifiScanResult& b) {
    return a.rssi > b.rssi;
  });
  WiFi.scanDelete();
}

void WifiManager::tryKnownNetworks() {
  if (savedCount_ == 0) return;

  // If scan data is available, put the strongest known network first.
  size_t best = savedCount_;
  int32_t bestRssi = -128;
  for (size_t i = 0; i < scanCount_; ++i) {
    const int idx = savedIndex(scan_[i].ssid);
    if (idx >= 0 &&
        !(enterpriseAuthRejected_ &&
          saved_[idx].ssid == enterpriseAuthRejectedSsid_) &&
        scan_[i].rssi > bestRssi) {
      best = static_cast<size_t>(idx);
      bestRssi = scan_[i].rssi;
    }
  }
  if (best == savedCount_) {
    for (size_t i = 0; i < savedCount_; ++i) {
      if (!(enterpriseAuthRejected_ &&
            saved_[i].ssid == enterpriseAuthRejectedSsid_)) {
        best = i;
        break;
      }
    }
  }
  if (best == savedCount_) return;
  reconnectIndex_ = best;
  reconnectAttempts_ = 0;
  startConnection(best);
}

bool WifiManager::connectSaved(size_t index) {
  if (index >= savedCount_) return false;
  reconnectIndex_ = index;
  reconnectAttempts_ = 0;
  enterpriseAuthRejected_ = false;
  enterpriseAuthRejectedSsid_.clear();
  WiFi.setAutoReconnect(true);
  return startConnection(index);
}

bool WifiManager::startConnection(size_t index) {
  if (saved_[index].security == WifiSecurity::EnterprisePeap) {
    return startEnterpriseConnection(saved_[index]);
  }
  // Do not leave an old enterprise profile active when switching back to a
  // normal WPA/WPA2 network. This matches the Arduino PEAP wrapper used
  // below while using the current ESP-IDF EAP API.
  const esp_err_t disabled = esp_wifi_sta_enterprise_disable();
  if (disabled != ESP_OK) {
    Serial.printf("[wifi] enterprise disable: %s\n", esp_err_to_name(disabled));
  }
  WiFi.disconnect();
  WiFi.begin(saved_[index].ssid.c_str(), saved_[index].password.c_str());
  connectStartedMs_ = millis();
  needsSetup_ = false;
  return true;
}

bool WifiManager::addAndConnect(const String& ssid, const String& password) {
  int index = savedIndex(ssid);
  if (index < 0) {
    if (savedCount_ >= kMaxWifiNetworks) return false;
    index = static_cast<int>(savedCount_++);
  }
  saved_[index].ssid = ssid;
  saved_[index].password = password;
  saved_[index].username = "";
  saved_[index].security = password.isEmpty() ? WifiSecurity::Open
                                               : WifiSecurity::Personal;
  store_.saveWifiNetworks(saved_, savedCount_);
  return connectSaved(static_cast<size_t>(index));
}

void WifiManager::onWifiEvent(arduino_event_id_t event,
                              arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    // The numeric reason comes from ESP-IDF and is safe to share for support;
    // do not print EAP credentials or the user identity.
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    lastDisconnectReason_ = reason;
    Serial.printf("[wifi] station disconnected (reason=%u)\n", reason);
  }
}

bool WifiManager::addEnterpriseAndConnect(const String& ssid,
                                          const String& username,
                                          const String& password) {
  if (ssid.isEmpty() || username.isEmpty() || password.isEmpty()) return false;
  int index = savedIndex(ssid);
  if (index < 0) {
    if (savedCount_ >= kMaxWifiNetworks) return false;
    index = static_cast<int>(savedCount_++);
  }
  saved_[index].ssid = ssid;
  saved_[index].username = username;
  saved_[index].password = password;
  saved_[index].security = WifiSecurity::EnterprisePeap;
  store_.saveWifiNetworks(saved_, savedCount_);
  return connectSaved(static_cast<size_t>(index));
}

bool WifiManager::startEnterpriseConnection(const WifiNetwork& network) {
  // Use Arduino's PEAP entry point rather than configuring the raw IDF EAP
  // client ourselves. It is the same path used by the known-working
  // gcores-pocket-player firmware on this Cardputer ADV and keeps the
  // Arduino-ESP32/ESP-IDF compatibility calls together. For PEAP/MSCHAPv2,
  // the common username is both the outer identity and inner username.
  WiFi.mode(WIFI_STA);
  lastDisconnectReason_ = 0;
  // Keep the no-RTC compatibility behavior explicit. This project currently
  // does not provision a CA certificate, matching the known-working legacy
  // firmware, so server-certificate date checking cannot be made authoritative
  // until CA provisioning and clock setup are added.
  const esp_err_t timeCheck = esp_eap_client_set_disable_time_check(true);
  if (timeCheck != ESP_OK) {
    Serial.printf("[wifi] enterprise time-check setup: %s\n",
                  esp_err_to_name(timeCheck));
  }
  const wl_status_t requested = WiFi.begin(
      network.ssid.c_str(), WPA2_AUTH_PEAP, network.username.c_str(),
      network.username.c_str(), network.password.c_str());
  connectStartedMs_ = millis();
  needsSetup_ = false;
  // Do not print the EAP identity or password: enterprise usernames commonly
  // identify a person. The status value is enough to diagnose setup failures.
  Serial.printf("[wifi] enterprise PEAP connection requested for \"%s\" (status=%d)\n",
                network.ssid.c_str(), static_cast<int>(requested));
  return true;
}

bool WifiManager::forget(const String& ssid) {
  const int index = savedIndex(ssid);
  if (index < 0) return false;
  const bool wasCurrent = connected() && WiFi.SSID() == ssid;
  for (size_t i = static_cast<size_t>(index); i + 1 < savedCount_; ++i) {
    saved_[i] = saved_[i + 1];
  }
  --savedCount_;
  store_.saveWifiNetworks(saved_, savedCount_);
  for (size_t i = 0; i < scanCount_; ++i) {
    if (scan_[i].ssid == ssid) scan_[i].saved = false;
  }
  if (wasCurrent) WiFi.disconnect();
  if (enterpriseAuthRejectedSsid_ == ssid) {
    enterpriseAuthRejected_ = false;
    enterpriseAuthRejectedSsid_.clear();
  }
  if (savedCount_ == 0) {
    needsSetup_ = true;
    startScan();
  }
  return true;
}

int WifiManager::savedIndex(const String& ssid) const {
  for (size_t i = 0; i < savedCount_; ++i) {
    if (saved_[i].ssid == ssid) return static_cast<int>(i);
  }
  return -1;
}

bool WifiManager::isSaved(const String& ssid) const {
  return savedIndex(ssid) >= 0;
}

}  // namespace cardbridge
