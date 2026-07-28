# Enterprise-compatible WPA supplicant

`libwpa_supplicant.a` is built from the ESP-IDF libraries pinned by
pioarduino platform `54.03.21-2`:

- ESP-IDF library revision: `858a988d6e`
- package version: `5.4.0+sha.858a988d6e`
- target: ESP32-S3
- configuration change: `CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT=n`
- SHA-256: `9c0cdc624d199f84678bb3dbdbfa3fecba21b42a4350f5d1325d8a19b28a7842`

ESP-IDF 5.4's Kconfig documents that MbedTLS 3 no longer supports TLS 1.0 or
1.1 and recommends disabling its Wi-Fi Enterprise TLS client when an older
server still needs those protocol versions. The resulting internal supplicant
client supports TLS 1.0 through TLS 1.2. HTTPS and Windows Bridge traffic do
not use this archive.

The corresponding source is Espressif ESP-IDF `release/v5.4`, licensed under
Apache-2.0. See `LICENSE-ESP-IDF-APACHE-2.0.txt` in this directory and the
repository's `THIRD_PARTY_NOTICES.md`.
