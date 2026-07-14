#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "app/Models.h"

namespace net {

constexpr uint16_t kDefaultBridgePort = 8765;

struct StoredSettings {
  app::BridgeEndpoint bridge;
  app::DisplayMode mode = app::DisplayMode::Auto;
};

// Accepts an IPv4 address or DNS/mDNS name, optionally followed by :port.
// The output is only changed when the entire input is valid.
bool parseBridgeEndpoint(const char* text, app::BridgeEndpoint& output);

// Uses an application-owned NVS namespace and never resets the ESP32 Wi-Fi
// namespace. Each operation opens Preferences only for its own short lifetime.
// The mutex serializes callers on the Arduino and HTTP tasks.
class SettingsStore {
 public:
  SettingsStore();
  ~SettingsStore();

  SettingsStore(const SettingsStore&) = delete;
  SettingsStore& operator=(const SettingsStore&) = delete;

  bool load(StoredSettings& output);
  bool saveBridge(const app::BridgeEndpoint& endpoint);
  bool saveDisplayMode(app::DisplayMode mode);

 private:
  bool take();
  void give();

  SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace net
