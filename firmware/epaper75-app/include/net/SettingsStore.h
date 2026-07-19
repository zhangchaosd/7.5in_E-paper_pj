#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "app/Models.h"

namespace net {

constexpr uint16_t kDefaultAgentPort = 8765;
constexpr size_t kAgentTokenSize = 96;
constexpr size_t kAgentInstallationIdSize = 48;

// Connection details deliberately live outside UiModel so credentials and
// LAN addresses cannot leak through renderer snapshots or the device API.
struct AgentEndpoint {
  char host[app::kHostTextSize] = {};
  uint16_t port = kDefaultAgentPort;

  bool configured() const { return host[0] != '\0'; }
};

struct AgentFleetSettings {
  AgentEndpoint endpoints[app::kAgentDeviceCount];
  // Learned during the first authenticated pairing and then pinned across
  // reboots. Empty means the USB-provisioned endpoint is not paired yet.
  char installationIds[app::kAgentDeviceCount][kAgentInstallationIdSize] = {};
  char token[kAgentTokenSize] = {};
  uint8_t primaryIndex = 0;

  size_t configuredCount() const;
  bool hasToken() const;
  bool ready() const;
};

struct StoredSettings {
  AgentFleetSettings agents;
  app::DisplayMode mode = app::DisplayMode::Auto;
};

enum class PinInstallationIdResult : uint8_t {
  Pinned = 0,
  AlreadyPinned,
  Superseded,
  Invalid,
  StorageError,
};

// Accepts an IPv4 address or DNS/mDNS name, optionally followed by :port.
// The output is only changed when the entire input is valid.
bool parseAgentEndpoint(const char* text, AgentEndpoint& output);

// Bearer tokens are kept to one non-empty visible-ASCII header value. The
// fixed buffer includes its trailing NUL.
bool validateAgentToken(const char* token);

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
  bool saveAgents(const AgentFleetSettings& agents);
  // Atomically pins one identity only if the current endpoint/token/primary
  // configuration still matches expected. This prevents a completed poll from
  // overwriting a newer USB provisioning command.
  PinInstallationIdResult pinInstallationId(
      size_t slotIndex, const AgentFleetSettings& expected,
      const char* installationId, AgentFleetSettings& updated);
  bool saveDisplayMode(app::DisplayMode mode);

 private:
  bool take();
  void give();

  SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace net
