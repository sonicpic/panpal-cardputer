#include "settings_store.h"

#include <nvs.h>

namespace cardbridge {
namespace {

String keyFor(const char* prefix, size_t index) {
  return String(prefix) + String(index);
}

// One byte is the authoritative connection selection. Unlike the legacy
// link_mode + link_set pair, this cannot be left half-updated by a reset.
constexpr uint8_t kLinkUnselected = 0;
constexpr uint8_t kLinkWifi = 1;
constexpr uint8_t kLinkBluetooth = 2;

uint8_t encodeLinkConfig(ConnectionMode mode, bool chosen) {
  if (!chosen) return kLinkUnselected;
  return mode == ConnectionMode::Bluetooth ? kLinkBluetooth : kLinkWifi;
}

bool eraseKey(nvs_handle_t handle, const char* key) {
  const esp_err_t result = nvs_erase_key(handle, key);
  return result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND;
}

bool setStringOrErase(nvs_handle_t handle, const char* key,
                      const String& value) {
  return value.isEmpty() ? eraseKey(handle, key)
                         : nvs_set_str(handle, key, value.c_str()) == ESP_OK;
}

bool setU8OrErase(nvs_handle_t handle, const char* key, uint8_t value,
                  uint8_t defaultValue) {
  return value == defaultValue ? eraseKey(handle, key)
                               : nvs_set_u8(handle, key, value) == ESP_OK;
}

bool setU16OrErase(nvs_handle_t handle, const char* key, uint16_t value,
                   uint16_t defaultValue) {
  return value == defaultValue ? eraseKey(handle, key)
                               : nvs_set_u16(handle, key, value) == ESP_OK;
}

bool commitAndClose(nvs_handle_t handle, bool ok) {
  if (ok) ok = nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

}  // namespace

bool SettingsStore::begin() {
  ready_ = preferences_.begin("cardbridge", false);
  if (ready_ && !compactIfNeeded()) {
    Serial.println("[nvs] ERROR: PanPal settings compaction failed");
  }
  return ready_;
}

bool SettingsStore::compactIfNeeded() {
  nvs_stats_t stats{};
  if (nvs_get_stats("nvs", &stats) != ESP_OK) return true;

  nvs_handle_t handle = 0;
  size_t namespaceEntries = 0;
  if (nvs_open("cardbridge", NVS_READONLY, &handle) == ESP_OK) {
    nvs_get_used_entry_count(handle, &namespaceEntries);
    nvs_close(handle);
  }
  Serial.printf("[nvs] total=%u used=%u free=%u cardbridge=%u\n",
                static_cast<unsigned>(stats.total_entries),
                static_cast<unsigned>(stats.used_entries),
                static_cast<unsigned>(stats.free_entries),
                static_cast<unsigned>(namespaceEntries));
  if (stats.free_entries >= 48 || namespaceEntries < 8) return true;

  // M5Launcher and installed apps share the default NVS partition.  Preserve
  // every PanPal value in RAM, clear only our namespace, then rewrite the live
  // records without stale keys.  Never erase the whole NVS partition because
  // that would destroy Launcher and other applications' settings.
  WifiNetwork wifi[kMaxWifiNetworks];
  PairedMac paired[kMaxPairedMacs];
  const size_t wifiCount = loadWifiNetworks(wifi, kMaxWifiNetworks);
  const size_t pairedCount = loadPairedMacs(paired, kMaxPairedMacs);
  const DeviceSettings settings = loadSettings();
  const bool wifiMigration = wifiDriverMigrationComplete();

  Serial.println("[nvs] low space; compacting PanPal settings namespace");
  if (!preferences_.clear()) {
    Serial.println("[nvs] ERROR: namespace compaction could not clear old keys");
    return false;
  }
  const bool wifiOk = saveWifiNetworks(wifi, wifiCount);
  const bool pairOk = savePairedMacs(paired, pairedCount);
  const bool settingsOk = saveSettings(settings);
  const bool migrationOk = !wifiMigration || markWifiDriverMigrationComplete();
  const bool ok = wifiOk && pairOk && settingsOk && migrationOk;

  nvs_stats_t after{};
  if (nvs_get_stats("nvs", &after) == ESP_OK) {
    Serial.printf("[nvs] compacted free=%u result=%s\n",
                  static_cast<unsigned>(after.free_entries),
                  ok ? "ok" : "FAILED");
  }
  return ok;
}

size_t SettingsStore::loadWifiNetworks(WifiNetwork* out, size_t capacity) {
  if (!ready_) return 0;
  const size_t count = min<size_t>(preferences_.getUChar("wifi_n", 0), capacity);
  for (size_t i = 0; i < count; ++i) {
    out[i].ssid = preferences_.getString(keyFor("ws", i).c_str(), "");
    out[i].password = preferences_.getString(keyFor("wp", i).c_str(), "");
    out[i].username = preferences_.getString(keyFor("wu", i).c_str(), "");
    out[i].security = preferences_.getUChar(keyFor("wt", i).c_str(),
                                             static_cast<uint8_t>(WifiSecurity::Personal))
                          == static_cast<uint8_t>(WifiSecurity::EnterprisePeap)
                          ? WifiSecurity::EnterprisePeap
                          : out[i].password.isEmpty() ? WifiSecurity::Open
                                                       : WifiSecurity::Personal;
  }
  return count;
}

bool SettingsStore::saveWifiNetworks(const WifiNetwork* networks, size_t count) {
  if (!ready_ || count > kMaxWifiNetworks) return false;
  const size_t previous = min<size_t>(preferences_.getUChar("wifi_n", 0),
                                      kMaxWifiNetworks);
  nvs_handle_t handle = 0;
  if (nvs_open("cardbridge", NVS_READWRITE, &handle) != ESP_OK) return false;
  bool ok = nvs_set_u8(handle, "wifi_n", static_cast<uint8_t>(count)) == ESP_OK;
  for (size_t i = 0; i < count; ++i) {
    ok = setStringOrErase(handle, keyFor("ws", i).c_str(), networks[i].ssid) && ok;
    ok = setStringOrErase(handle, keyFor("wp", i).c_str(), networks[i].password) && ok;
    ok = setStringOrErase(handle, keyFor("wu", i).c_str(), networks[i].username) && ok;
    ok = (nvs_set_u8(handle, keyFor("wt", i).c_str(),
                     static_cast<uint8_t>(networks[i].security)) == ESP_OK) &&
         ok;
  }
  for (size_t i = count; i < previous; ++i) {
    ok = eraseKey(handle, keyFor("ws", i).c_str()) && ok;
    ok = eraseKey(handle, keyFor("wp", i).c_str()) && ok;
    ok = eraseKey(handle, keyFor("wu", i).c_str()) && ok;
    ok = eraseKey(handle, keyFor("wt", i).c_str()) && ok;
  }
  return commitAndClose(handle, ok);
}

bool SettingsStore::wifiDriverMigrationComplete() {
  return ready_ && preferences_.getBool("wifi_drv_v1", false);
}

bool SettingsStore::markWifiDriverMigrationComplete() {
  if (!ready_) return false;
  nvs_handle_t handle = 0;
  if (nvs_open("cardbridge", NVS_READWRITE, &handle) != ESP_OK) return false;
  const bool ok = nvs_set_u8(handle, "wifi_drv_v1", 1) == ESP_OK;
  return commitAndClose(handle, ok);
}

size_t SettingsStore::loadPairedMacs(PairedMac* out, size_t capacity) {
  if (!ready_) return 0;
  const size_t count = min<size_t>(preferences_.getUChar("mac_n", 0), capacity);
  for (size_t i = 0; i < count; ++i) {
    out[i].id = preferences_.getString(keyFor("mi", i).c_str(), "");
    out[i].name = preferences_.getString(keyFor("mn", i).c_str(), "Computer");
    out[i].token = preferences_.getString(keyFor("mt", i).c_str(), "");
    out[i].transport =
        preferences_.getUChar(keyFor("mx", i).c_str(), 0) == 1
            ? ConnectionMode::Bluetooth
            : ConnectionMode::Wifi;
    if (out[i].transport == ConnectionMode::Bluetooth) {
      out[i].bleAddress = preferences_.getString(keyFor("mb", i).c_str(), "");
      out[i].bleAddressType = preferences_.getUChar(keyFor("mc", i).c_str(), 0);
    } else {
      out[i].bleAddress.clear();
      out[i].bleAddressType = 0;
    }
  }
  return count;
}

bool SettingsStore::savePairedMacs(const PairedMac* macs, size_t count) {
  if (!ready_ || count > kMaxPairedMacs) return false;
  const size_t previous = min<size_t>(preferences_.getUChar("mac_n", 0),
                                      kMaxPairedMacs);
  nvs_handle_t handle = 0;
  if (nvs_open("cardbridge", NVS_READWRITE, &handle) != ESP_OK) return false;
  bool ok = nvs_set_u8(handle, "mac_n", static_cast<uint8_t>(count)) == ESP_OK;
  for (size_t i = 0; i < count; ++i) {
    ok = setStringOrErase(handle, keyFor("mi", i).c_str(), macs[i].id) && ok;
    ok = setStringOrErase(handle, keyFor("mn", i).c_str(), macs[i].name) && ok;
    ok = setStringOrErase(handle, keyFor("mt", i).c_str(), macs[i].token) && ok;
    if (macs[i].transport == ConnectionMode::Bluetooth) {
      ok = (nvs_set_u8(handle, keyFor("mx", i).c_str(), 1) == ESP_OK) && ok;
      ok = setStringOrErase(handle, keyFor("mb", i).c_str(), macs[i].bleAddress) && ok;
      ok = (nvs_set_u8(handle, keyFor("mc", i).c_str(),
                       macs[i].bleAddressType) == ESP_OK) &&
           ok;
    } else {
      ok = eraseKey(handle, keyFor("mx", i).c_str()) && ok;
      ok = eraseKey(handle, keyFor("mb", i).c_str()) && ok;
      ok = eraseKey(handle, keyFor("mc", i).c_str()) && ok;
    }
  }
  for (size_t i = count; i < previous; ++i) {
    ok = eraseKey(handle, keyFor("mi", i).c_str()) && ok;
    ok = eraseKey(handle, keyFor("mn", i).c_str()) && ok;
    ok = eraseKey(handle, keyFor("mt", i).c_str()) && ok;
    ok = eraseKey(handle, keyFor("mx", i).c_str()) && ok;
    ok = eraseKey(handle, keyFor("mb", i).c_str()) && ok;
    ok = eraseKey(handle, keyFor("mc", i).c_str()) && ok;
  }
  return commitAndClose(handle, ok);
}

DeviceSettings SettingsStore::loadSettings() {
  DeviceSettings settings;
  if (!ready_) return settings;
  settings.micMuted = preferences_.getBool("mic_mute", false);
  settings.sendEnterAfterVoice = preferences_.getBool("voice_ent", false);
  settings.notificationTone = preferences_.getUChar("notify_t", 1);
  if (settings.notificationTone > 3) settings.notificationTone = 1;
  settings.notificationVolume = preferences_.getUChar("notify_v", 128);
  settings.typelessFunctionKey = preferences_.getUChar("voice_f", 13);
  if (settings.typelessFunctionKey < 13 || settings.typelessFunctionKey > 16) {
    settings.typelessFunctionKey = 13;
  }
  settings.brightness = preferences_.getUChar("bright", kDefaultBrightness);
  settings.screenTimeoutSec =
      preferences_.getUShort("screen_s", kDefaultScreenTimeoutSec);
  settings.lastMacId = preferences_.getString("last_mac", "");
  const uint8_t linkConfig = preferences_.getUChar("link_cfg", kLinkUnselected);
  if (linkConfig == kLinkWifi || linkConfig == kLinkBluetooth) {
    settings.connectionMode = linkConfig == kLinkBluetooth
                                  ? ConnectionMode::Bluetooth
                                  : ConnectionMode::Wifi;
    settings.connectionModeChosen = true;
  } else {
    settings.connectionMode = preferences_.getUChar("link_mode", 0) == 1
                                  ? ConnectionMode::Bluetooth
                                  : ConnectionMode::Wifi;
    settings.connectionModeChosen = preferences_.getBool("link_set", false);
  }
  if (!settings.connectionModeChosen &&
      (preferences_.getUChar("wifi_n", 0) > 0 ||
       preferences_.getUChar("mac_n", 0) > 0 ||
       preferences_.getBool("wifi_drv_v1", false))) {
    // Existing installations predate link_set and must remain on Wi-Fi.
    settings.connectionMode = ConnectionMode::Wifi;
    settings.connectionModeChosen = true;
  }
  if (settings.connectionModeChosen && linkConfig == kLinkUnselected) {
    // Migrate an old two-key selection to the atomic representation. A failed
    // migration is harmless because the legacy keys remain intact.
    saveConnectionMode(settings.connectionMode);
  }
  return settings;
}

bool SettingsStore::saveSettings(const DeviceSettings& settings) {
  if (!ready_) return false;
  nvs_handle_t handle = 0;
  if (nvs_open("cardbridge", NVS_READWRITE, &handle) != ESP_OK) return false;
  bool ok = setU8OrErase(handle, "mic_mute", settings.micMuted ? 1 : 0, 0);
  ok = setU8OrErase(handle, "voice_ent",
                    settings.sendEnterAfterVoice ? 1 : 0, 0) && ok;
  ok = setU8OrErase(handle, "notify_t", settings.notificationTone, 1) && ok;
  ok = setU8OrErase(handle, "notify_v", settings.notificationVolume, 128) && ok;
  ok = setU8OrErase(handle, "voice_f", settings.typelessFunctionKey, 13) && ok;
  ok = setU8OrErase(handle, "bright", settings.brightness,
                    kDefaultBrightness) && ok;
  ok = setU16OrErase(handle, "screen_s", settings.screenTimeoutSec,
                     kDefaultScreenTimeoutSec) && ok;
  ok = setStringOrErase(handle, "last_mac", settings.lastMacId) && ok;

  // Keep the legacy pair for downgrade compatibility. link_cfg is the atomic,
  // authoritative value used by current firmware.
  if (settings.connectionModeChosen) {
    ok = (nvs_set_u8(handle, "link_mode",
                     static_cast<uint8_t>(settings.connectionMode)) == ESP_OK) &&
         ok;
    ok = (nvs_set_u8(handle, "link_set", 1) == ESP_OK) && ok;
  } else {
    ok = eraseKey(handle, "link_mode") && ok;
    ok = eraseKey(handle, "link_set") && ok;
  }
  const uint8_t linkConfig =
      encodeLinkConfig(settings.connectionMode, settings.connectionModeChosen);
  ok = (nvs_set_u8(handle, "link_cfg", linkConfig) == ESP_OK) && ok;
  if (ok) ok = nvs_commit(handle) == ESP_OK;
  uint8_t verified = 0xFF;
  if (ok) ok = nvs_get_u8(handle, "link_cfg", &verified) == ESP_OK &&
               verified == linkConfig;
  nvs_close(handle);
  return ok;
}

bool SettingsStore::saveConnectionMode(ConnectionMode mode) {
  if (!ready_) return false;
  nvs_handle_t handle = 0;
  if (nvs_open("cardbridge", NVS_READWRITE, &handle) != ESP_OK) return false;
  // Keep the old keys for downgrade compatibility, but verify the atomic key
  // before the caller is allowed to reboot.
  bool ok = nvs_set_u8(handle, "link_mode", static_cast<uint8_t>(mode)) == ESP_OK;
  ok = (nvs_set_u8(handle, "link_set", 1) == ESP_OK) && ok;
  const uint8_t linkConfig = encodeLinkConfig(mode, true);
  ok = (nvs_set_u8(handle, "link_cfg", linkConfig) == ESP_OK) && ok;
  if (ok) ok = nvs_commit(handle) == ESP_OK;
  uint8_t verified = 0xFF;
  if (ok) ok = nvs_get_u8(handle, "link_cfg", &verified) == ESP_OK &&
               verified == linkConfig;
  nvs_close(handle);
  return ok;
}

}  // namespace cardbridge
