#include "net/SettingsStore.h"

#include <Preferences.h>
#include <ctype.h>
#include <string.h>

namespace net {
namespace {

constexpr char kNamespace[] = "ep75_p1";
constexpr char kAgentsKey[] = "agent_fleet";
constexpr char kModeKey[] = "mode";
constexpr TickType_t kMutexWait = pdMS_TO_TICKS(1500);

constexpr uint8_t kBlobMagic[] = {'E', 'P', 'A', 'F'};
constexpr uint16_t kBlobVersion = 2;
constexpr size_t kBlobHeaderSize = 12;
constexpr size_t kEndpointBlobSize = 1 + 2 + app::kHostTextSize;
constexpr size_t kBlobPayloadSize =
    app::kAgentDeviceCount *
        (kEndpointBlobSize + kAgentInstallationIdSize) +
    kAgentTokenSize + 1;
constexpr size_t kBlobSize = kBlobHeaderSize + kBlobPayloadSize;

static_assert(app::kAgentDeviceCount > 0,
              "an agent fleet must contain at least one device");
static_assert(app::kAgentDeviceCount <= UINT8_MAX,
              "primaryIndex must address every fleet endpoint");
static_assert(kBlobPayloadSize <= UINT16_MAX,
              "agent settings blob payload length must fit uint16_t");

void secureZero(void* memory, size_t size) {
  volatile uint8_t* cursor = static_cast<volatile uint8_t*>(memory);
  while (size-- > 0) *cursor++ = 0;
}

bool isHostCharacter(char value) {
  const unsigned char ch = static_cast<unsigned char>(value);
  return isalnum(ch) || value == '-' || value == '_' || value == '.';
}

void writeU16(uint8_t* destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value & 0xffU);
  destination[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
}

uint16_t readU16(const uint8_t* source) {
  return static_cast<uint16_t>(source[0]) |
         static_cast<uint16_t>(source[1]) << 8U;
}

void writeU32(uint8_t* destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value & 0xffU);
  destination[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
  destination[2] = static_cast<uint8_t>((value >> 16U) & 0xffU);
  destination[3] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

uint32_t readU32(const uint8_t* source) {
  return static_cast<uint32_t>(source[0]) |
         static_cast<uint32_t>(source[1]) << 8U |
         static_cast<uint32_t>(source[2]) << 16U |
         static_cast<uint32_t>(source[3]) << 24U;
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = -(crc & 1U);
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return crc;
}

uint32_t blobChecksum(const uint8_t* blob) {
  uint32_t crc = 0xffffffffU;
  crc = crc32Update(crc, blob, 8);
  crc = crc32Update(crc, blob + kBlobHeaderSize, kBlobPayloadSize);
  return crc ^ 0xffffffffU;
}

bool endpointIsValid(const AgentEndpoint& endpoint) {
  if (!endpoint.configured() ||
      memchr(endpoint.host, '\0', sizeof(endpoint.host)) == nullptr) {
    return false;
  }

  char combined[app::kHostTextSize + 7] = {};
  const int count = snprintf(combined, sizeof(combined), "%s:%u", endpoint.host,
                             static_cast<unsigned>(endpoint.port));
  if (count <= 0 || static_cast<size_t>(count) >= sizeof(combined)) return false;

  AgentEndpoint parsed;
  return parseAgentEndpoint(combined, parsed) &&
         strcmp(parsed.host, endpoint.host) == 0 &&
         parsed.port == endpoint.port;
}

bool installationIdIsValid(const char* identity) {
  if (identity == nullptr ||
      memchr(identity, '\0', kAgentInstallationIdSize) == nullptr) {
    return false;
  }
  const size_t length = strlen(identity);
  if (length == 0) return true;
  for (size_t index = 0; index < length; ++index) {
    const unsigned char ch = static_cast<unsigned char>(identity[index]);
    if (ch < 0x21U || ch > 0x7eU) return false;
  }
  return true;
}

bool fleetIsValid(const AgentFleetSettings& agents) {
  if (agents.primaryIndex >= app::kAgentDeviceCount ||
      !validateAgentToken(agents.token)) {
    return false;
  }
  for (size_t index = 0; index < app::kAgentDeviceCount; ++index) {
    if (!endpointIsValid(agents.endpoints[index]) ||
        !installationIdIsValid(agents.installationIds[index])) {
      return false;
    }
  }
  return true;
}

bool connectionSettingsMatch(const AgentFleetSettings& lhs,
                             const AgentFleetSettings& rhs) {
  if (lhs.primaryIndex != rhs.primaryIndex ||
      strcmp(lhs.token, rhs.token) != 0) {
    return false;
  }
  for (size_t index = 0; index < app::kAgentDeviceCount; ++index) {
    if (lhs.endpoints[index].port != rhs.endpoints[index].port ||
        strcmp(lhs.endpoints[index].host, rhs.endpoints[index].host) != 0) {
      return false;
    }
  }
  return true;
}

void encodeAgents(const AgentFleetSettings& agents, uint8_t* blob) {
  memset(blob, 0, kBlobSize);
  memcpy(blob, kBlobMagic, sizeof(kBlobMagic));
  writeU16(blob + 4, kBlobVersion);
  writeU16(blob + 6, static_cast<uint16_t>(kBlobPayloadSize));

  size_t offset = kBlobHeaderSize;
  for (size_t index = 0; index < app::kAgentDeviceCount; ++index) {
    const AgentEndpoint& endpoint = agents.endpoints[index];
    blob[offset++] = endpoint.configured() ? 1 : 0;
    writeU16(blob + offset, endpoint.port);
    offset += 2;
    memcpy(blob + offset, endpoint.host, app::kHostTextSize);
    offset += app::kHostTextSize;
    memcpy(blob + offset, agents.installationIds[index],
           kAgentInstallationIdSize);
    offset += kAgentInstallationIdSize;
  }
  memcpy(blob + offset, agents.token, kAgentTokenSize);
  offset += kAgentTokenSize;
  blob[offset] = agents.primaryIndex;

  writeU32(blob + 8, blobChecksum(blob));
}

bool decodeAgents(const uint8_t* blob, AgentFleetSettings& output) {
  if (memcmp(blob, kBlobMagic, sizeof(kBlobMagic)) != 0 ||
      readU16(blob + 4) != kBlobVersion ||
      readU16(blob + 6) != kBlobPayloadSize ||
      readU32(blob + 8) != blobChecksum(blob)) {
    return false;
  }

  AgentFleetSettings decoded;
  size_t offset = kBlobHeaderSize;
  for (size_t index = 0; index < app::kAgentDeviceCount; ++index) {
    const uint8_t configured = blob[offset++];
    if (configured != 1) return false;
    decoded.endpoints[index].port = readU16(blob + offset);
    offset += 2;
    memcpy(decoded.endpoints[index].host, blob + offset,
           app::kHostTextSize);
    offset += app::kHostTextSize;
    if (decoded.endpoints[index].host[app::kHostTextSize - 1] != '\0' ||
        !endpointIsValid(decoded.endpoints[index])) {
      return false;
    }
    memcpy(decoded.installationIds[index], blob + offset,
           kAgentInstallationIdSize);
    offset += kAgentInstallationIdSize;
    if (!installationIdIsValid(decoded.installationIds[index])) return false;
  }

  memcpy(decoded.token, blob + offset, kAgentTokenSize);
  offset += kAgentTokenSize;
  if (decoded.token[kAgentTokenSize - 1] != '\0') return false;
  decoded.primaryIndex = blob[offset];
  if (!fleetIsValid(decoded)) return false;

  output = decoded;
  secureZero(decoded.token, sizeof(decoded.token));
  return true;
}

}  // namespace

size_t AgentFleetSettings::configuredCount() const {
  size_t count = 0;
  for (const AgentEndpoint& endpoint : endpoints) {
    if (endpoint.configured()) ++count;
  }
  return count;
}

bool AgentFleetSettings::hasToken() const {
  return validateAgentToken(token);
}

bool AgentFleetSettings::ready() const {
  return primaryIndex < app::kAgentDeviceCount && hasToken() &&
         configuredCount() == app::kAgentDeviceCount;
}

bool parseAgentEndpoint(const char* text, AgentEndpoint& output) {
  if (text == nullptr) return false;

  while (isspace(static_cast<unsigned char>(*text))) ++text;
  size_t length = strlen(text);
  while (length > 0 &&
         isspace(static_cast<unsigned char>(text[length - 1]))) {
    --length;
  }
  if (length == 0 || length >= app::kHostTextSize + 7) return false;

  char input[app::kHostTextSize + 7] = {};
  memcpy(input, text, length);
  input[length] = '\0';

  // Schemes, paths, user-info and IPv6 bracket syntax are deliberately not
  // accepted: this field is a LAN host, not a URL.
  if (strstr(input, "://") != nullptr || strpbrk(input, "/\\?#@[]") != nullptr) {
    return false;
  }

  char* colon = strrchr(input, ':');
  if (colon != nullptr && strchr(input, ':') != colon) return false;

  uint16_t port = kDefaultAgentPort;
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

  AgentEndpoint parsedEndpoint;
  app::copyText(parsedEndpoint.host, input);
  parsedEndpoint.port = port;
  output = parsedEndpoint;
  return true;
}

bool validateAgentToken(const char* token) {
  if (token == nullptr) return false;
  const size_t length = strnlen(token, kAgentTokenSize);
  if (length == 0 || length >= kAgentTokenSize) return false;
  for (size_t index = 0; index < length; ++index) {
    const unsigned char ch = static_cast<unsigned char>(token[index]);
    if (ch < 0x21 || ch > 0x7e) return false;
  }
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
  Preferences preferences;
  const bool opened = preferences.begin(kNamespace, false);
  if (opened) {
    if (preferences.getBytesLength(kAgentsKey) == kBlobSize) {
      uint8_t blob[kBlobSize] = {};
      if (preferences.getBytes(kAgentsKey, blob, sizeof(blob)) == sizeof(blob)) {
        AgentFleetSettings decoded;
        if (decodeAgents(blob, decoded)) {
          loaded.agents = decoded;
          secureZero(decoded.token, sizeof(decoded.token));
        }
      }
      secureZero(blob, sizeof(blob));
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
  secureZero(loaded.agents.token, sizeof(loaded.agents.token));
  return true;
}

bool SettingsStore::saveAgents(const AgentFleetSettings& agents) {
  if (!fleetIsValid(agents) || !take()) return false;

  uint8_t blob[kBlobSize] = {};
  encodeAgents(agents, blob);

  Preferences preferences;
  const bool opened = preferences.begin(kNamespace, false);
  const bool saved = opened &&
                     preferences.putBytes(kAgentsKey, blob, sizeof(blob)) ==
                         sizeof(blob);
  if (opened) preferences.end();
  secureZero(blob, sizeof(blob));
  give();
  return saved;
}

PinInstallationIdResult SettingsStore::pinInstallationId(
    size_t slotIndex, const AgentFleetSettings& expected,
    const char* installationId, AgentFleetSettings& updated) {
  if (slotIndex >= app::kAgentDeviceCount ||
      !fleetIsValid(expected) || installationId == nullptr ||
      installationId[0] == '\0' || !installationIdIsValid(installationId)) {
    return PinInstallationIdResult::Invalid;
  }
  if (!take()) return PinInstallationIdResult::StorageError;

  PinInstallationIdResult result = PinInstallationIdResult::StorageError;
  Preferences preferences;
  const bool opened = preferences.begin(kNamespace, false);
  if (opened && preferences.getBytesLength(kAgentsKey) == kBlobSize) {
    uint8_t blob[kBlobSize] = {};
    AgentFleetSettings current;
    const bool loaded =
        preferences.getBytes(kAgentsKey, blob, sizeof(blob)) == sizeof(blob) &&
        decodeAgents(blob, current);
    secureZero(blob, sizeof(blob));

    if (loaded && connectionSettingsMatch(current, expected)) {
      const char* pinned = current.installationIds[slotIndex];
      if (pinned[0] != '\0') {
        result = strcmp(pinned, installationId) == 0
                     ? PinInstallationIdResult::AlreadyPinned
                     : PinInstallationIdResult::Superseded;
        updated = current;
      } else {
        strlcpy(current.installationIds[slotIndex], installationId,
                sizeof(current.installationIds[slotIndex]));
        encodeAgents(current, blob);
        const bool saved =
            preferences.putBytes(kAgentsKey, blob, sizeof(blob)) ==
            sizeof(blob);
        secureZero(blob, sizeof(blob));
        if (saved) {
          updated = current;
          result = PinInstallationIdResult::Pinned;
        }
      }
    } else if (loaded) {
      result = PinInstallationIdResult::Superseded;
    }
    secureZero(current.token, sizeof(current.token));
  }
  if (opened) preferences.end();
  give();
  return result;
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
