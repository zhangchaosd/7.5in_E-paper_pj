#include "net/SettingsStore.h"

#include <Preferences.h>
#include <ctype.h>
#include <string.h>

namespace net {
namespace {

constexpr char kNamespace[] = "ep75_p1";
constexpr char kBridgeKey[] = "bridge";
constexpr char kModeKey[] = "mode";
constexpr TickType_t kMutexWait = pdMS_TO_TICKS(1500);

bool isHostCharacter(char value) {
  const unsigned char ch = static_cast<unsigned char>(value);
  return isalnum(ch) || value == '-' || value == '_' || value == '.';
}

}  // namespace

bool parseBridgeEndpoint(const char* text, app::BridgeEndpoint& output) {
  if (text == nullptr) return false;

  while (isspace(static_cast<unsigned char>(*text))) ++text;
  size_t length = strlen(text);
  while (length > 0 &&
         isspace(static_cast<unsigned char>(text[length - 1]))) {
    --length;
  }
  if (length == 0 || length >= 80) return false;

  char input[80] = {};
  memcpy(input, text, length);
  input[length] = '\0';

  // Schemes, paths, user-info and IPv6 bracket syntax are deliberately not
  // accepted: this field is a LAN host, not a URL.
  if (strstr(input, "://") != nullptr || strpbrk(input, "/\\?#@[]") != nullptr) {
    return false;
  }

  char* colon = strrchr(input, ':');
  if (colon != nullptr && strchr(input, ':') != colon) return false;

  uint16_t port = kDefaultBridgePort;
  if (colon != nullptr) {
    *colon = '\0';
    const char* portText = colon + 1;
    if (*portText == '\0') return false;
    uint32_t parsed = 0;
    for (const char* cursor = portText; *cursor != '\0'; ++cursor) {
      if (!isdigit(static_cast<unsigned char>(*cursor))) return false;
      parsed = parsed * 10U + static_cast<uint32_t>(*cursor - '0');
      if (parsed > 65535U) return false;
    }
    if (parsed == 0) return false;
    port = static_cast<uint16_t>(parsed);
  }

  const size_t hostLength = strlen(input);
  if (hostLength == 0 || hostLength >= app::kHostTextSize || input[0] == '.' ||
      input[hostLength - 1] == '.') {
    return false;
  }
  for (size_t index = 0; index < hostLength; ++index) {
    if (!isHostCharacter(input[index])) return false;
  }

  app::BridgeEndpoint parsedEndpoint;
  app::copyText(parsedEndpoint.host, input);
  parsedEndpoint.port = port;
  output = parsedEndpoint;
  return true;
}

SettingsStore::SettingsStore() : mutex_(xSemaphoreCreateMutex()) {}

SettingsStore::~SettingsStore() {
  if (mutex_ != nullptr) {
    vSemaphoreDelete(mutex_);
    mutex_ = nullptr;
  }
}

bool SettingsStore::take() {
  return mutex_ != nullptr && xSemaphoreTake(mutex_, kMutexWait) == pdTRUE;
}

void SettingsStore::give() {
  if (mutex_ != nullptr) xSemaphoreGive(mutex_);
}

bool SettingsStore::load(StoredSettings& output) {
  if (!take()) return false;

  StoredSettings loaded;
  loaded.bridge.port = kDefaultBridgePort;
  Preferences preferences;
  const bool opened = preferences.begin(kNamespace, false);
  if (opened) {
    const String bridge = preferences.getString(kBridgeKey, "");
    app::BridgeEndpoint endpoint;
    if (bridge.length() > 0 && parseBridgeEndpoint(bridge.c_str(), endpoint)) {
      loaded.bridge = endpoint;
    }

    const uint8_t rawMode = preferences.getUChar(
        kModeKey, static_cast<uint8_t>(app::DisplayMode::Auto));
    if (rawMode <= static_cast<uint8_t>(app::DisplayMode::Codex)) {
      loaded.mode = static_cast<app::DisplayMode>(rawMode);
    }
    preferences.end();
  }

  give();
  if (!opened) return false;
  output = loaded;
  return true;
}

bool SettingsStore::saveBridge(const app::BridgeEndpoint& endpoint) {
  char combined[80] = {};
  const int count = snprintf(combined, sizeof(combined), "%s:%u", endpoint.host,
                             static_cast<unsigned>(endpoint.port));
  if (count <= 0 || static_cast<size_t>(count) >= sizeof(combined)) return false;

  app::BridgeEndpoint validated;
  if (!parseBridgeEndpoint(combined, validated) ||
      strcmp(validated.host, endpoint.host) != 0 ||
      validated.port != endpoint.port) {
    return false;
  }
  if (!take()) return false;

  Preferences preferences;
  const bool opened = preferences.begin(kNamespace, false);
  const bool saved = opened &&
                     preferences.putString(kBridgeKey, combined) == strlen(combined);
  if (opened) preferences.end();
  give();
  return saved;
}

bool SettingsStore::saveDisplayMode(app::DisplayMode mode) {
  const uint8_t rawMode = static_cast<uint8_t>(mode);
  if (rawMode > static_cast<uint8_t>(app::DisplayMode::Codex)) return false;
  if (!take()) return false;

  Preferences preferences;
  const bool opened = preferences.begin(kNamespace, false);
  const bool saved = opened && preferences.putUChar(kModeKey, rawMode) == 1;
  if (opened) preferences.end();
  give();
  return saved;
}

}  // namespace net
