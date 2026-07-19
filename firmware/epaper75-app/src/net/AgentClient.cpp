#include "net/AgentClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <string.h>

namespace net {
namespace {

constexpr uint16_t kConnectTimeoutMs = 1000;
constexpr uint16_t kReadTimeoutMs = 2200;
constexpr uint8_t kFailuresBeforeOffline = 3;
constexpr size_t kVersionPayloadLimit = 8 * 1024;
constexpr size_t kStatusPayloadLimit = 16 * 1024;
constexpr size_t kThreadsPayloadLimit = 24 * 1024;
constexpr size_t kRateLimitsPayloadLimit = 16 * 1024;
constexpr uint32_t kSettingsCheckPeriodMs = 2000;
constexpr uint32_t kQuotaPollPeriodMs = 60000;
constexpr uint32_t kQuotaCycleTimeoutMs = 45000;
constexpr uint32_t kOfflineBackoffBaseMs = 30000;
constexpr uint32_t kOfflineBackoffMaximumMs = 300000;
constexpr int32_t kWeeklyWindowMinimumMinutes = 6 * 24 * 60;
constexpr int32_t kWeeklyWindowMaximumMinutes = 8 * 24 * 60;
constexpr uint32_t kAgentTaskStackBytes = 12288;
constexpr UBaseType_t kAgentTaskPriority = 1;
constexpr BaseType_t kAgentTaskCore = 0;

constexpr const char* kDeviceLabels[app::kAgentDeviceCount] = {
    "MAC MINI", "DEV", "LG GRAM"};

class LimitedStream : public Stream {
 public:
  LimitedStream(Stream& source, size_t limit)
      : source_(source), remaining_(limit) {}

  int available() override {
    const int sourceAvailable = source_.available();
    if (remaining_ == 0) return 0;
    return sourceAvailable < static_cast<int>(remaining_)
               ? sourceAvailable
               : static_cast<int>(remaining_);
  }

  int read() override {
    if (remaining_ == 0) {
      exceeded_ = true;
      return -1;
    }
    const int value = source_.read();
    if (value >= 0) --remaining_;
    return value;
  }

  int peek() override {
    if (remaining_ == 0) {
      exceeded_ = true;
      return -1;
    }
    return source_.peek();
  }

  void flush() override { source_.flush(); }
  size_t write(uint8_t) override { return 0; }
  bool exceeded() const { return exceeded_; }

 private:
  Stream& source_;
  size_t remaining_;
  bool exceeded_ = false;
};

using JsonStreamParser = bool (*)(Stream& input, void* context);

struct GetResult {
  AgentPollResult result = AgentPollResult::HttpError;
  int httpCode = 0;
};

void secureZero(void* memory, size_t size) {
  volatile uint8_t* cursor = static_cast<volatile uint8_t*>(memory);
  while (size-- > 0) *cursor++ = 0;
}

bool sameSettings(const AgentFleetSettings& lhs,
                  const AgentFleetSettings& rhs) {
  if (lhs.primaryIndex != rhs.primaryIndex ||
      strcmp(lhs.token, rhs.token) != 0) {
    return false;
  }
  for (size_t i = 0; i < app::kAgentDeviceCount; ++i) {
    if (lhs.endpoints[i].port != rhs.endpoints[i].port ||
        strcmp(lhs.endpoints[i].host, rhs.endpoints[i].host) != 0 ||
        strcmp(lhs.installationIds[i], rhs.installationIds[i]) != 0) {
      return false;
    }
  }
  return true;
}

bool schemaMajorOne(const char* value) {
  if (value == nullptr || value[0] != '1') return false;
  return value[1] == '\0' || value[1] == '.';
}

bool readNonNegative(JsonVariantConst value, int32_t& output) {
  if (!value.is<int64_t>() && !value.is<uint64_t>()) return false;
  if (value.is<uint64_t>() && value.as<uint64_t>() > INT32_MAX) return false;
  const int64_t parsed = value.as<int64_t>();
  if (parsed < 0 || parsed > INT32_MAX) return false;
  output = static_cast<int32_t>(parsed);
  return true;
}

bool readEpochSeconds(JsonVariantConst value, int64_t& output) {
  if (!value.is<int64_t>() && !value.is<uint64_t>() && !value.is<double>()) {
    return false;
  }
  const double parsed = value.as<double>();
  if (!isfinite(parsed) || parsed < 946684800.0 ||
      parsed > static_cast<double>(INT64_MAX)) {
    return false;
  }
  output = static_cast<int64_t>(llround(parsed));
  return true;
}

app::WorkloadState parseWorkloadState(const char* value) {
  if (value == nullptr) return app::WorkloadState::Unknown;
  if (strcmp(value, "RUNNING") == 0) return app::WorkloadState::Running;
  if (strcmp(value, "WAITING_APPROVAL") == 0) {
    return app::WorkloadState::WaitingApproval;
  }
  if (strcmp(value, "WAITING_INPUT") == 0) {
    return app::WorkloadState::WaitingInput;
  }
  if (strcmp(value, "IDLE") == 0) return app::WorkloadState::Idle;
  if (strcmp(value, "ERROR") == 0) return app::WorkloadState::Error;
  return app::WorkloadState::Unknown;
}

int statePriority(app::WorkloadState value) {
  switch (value) {
    case app::WorkloadState::WaitingApproval:
      return 4;
    case app::WorkloadState::WaitingInput:
      return 3;
    case app::WorkloadState::Running:
      return 2;
    case app::WorkloadState::Error:
      return 1;
    default:
      return 0;
  }
}

void copyDisplayText(char* output, size_t capacity, const char* input) {
  if (capacity == 0) return;
  if (input == nullptr) input = "";

  size_t written = 0;
  bool pendingSpace = false;
  bool truncated = false;
  for (const unsigned char* cursor =
           reinterpret_cast<const unsigned char*>(input);
       *cursor != '\0'; ++cursor) {
    unsigned char value = *cursor;
    if (value == ' ' || value == '\t' || value == '\r' || value == '\n') {
      pendingSpace = written > 0;
      continue;
    }
    if (value >= 0x80U) {
      // The panel font is ASCII-only. Preserve the fact that a task has a
      // non-ASCII name by emitting one placeholder per UTF-8 code point.
      while ((cursor[1] & 0xc0U) == 0x80U) ++cursor;
      value = '?';
    }
    if (value < 32 || value > 126) continue;
    if (pendingSpace) {
      if (written + 1 >= capacity) {
        truncated = true;
        break;
      }
      output[written++] = ' ';
      pendingSpace = false;
    }
    if (written + 1 >= capacity) {
      truncated = true;
      break;
    }
    output[written++] = static_cast<char>(value);
  }
  while (written > 0 && output[written - 1] == ' ') --written;
  if (truncated && capacity >= 4) {
    const size_t marker = capacity - 4;
    if (written > marker) written = marker;
    output[written++] = '.';
    output[written++] = '.';
    output[written++] = '.';
  }
  output[written] = '\0';
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const int adjustedMonth =
      static_cast<int>(month) + (month > 2 ? -3 : 9);
  const unsigned dayOfYear =
      (153U * static_cast<unsigned>(adjustedMonth) + 2U) / 5U +
      day - 1;
  const unsigned dayOfEra =
      yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 +
         static_cast<int64_t>(dayOfEra) - 719468;
}

bool parseDigits(const char* value, size_t offset, size_t count, int& output) {
  int parsed = 0;
  for (size_t i = 0; i < count; ++i) {
    const char ch = value[offset + i];
    if (ch < '0' || ch > '9') return false;
    parsed = parsed * 10 + (ch - '0');
  }
  output = parsed;
  return true;
}

bool parseRfc3339Utc(const char* value, int64_t& output) {
  if (value == nullptr || strlen(value) < 20 || value[4] != '-' ||
      value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
      value[16] != ':') {
    return false;
  }
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (!parseDigits(value, 0, 4, year) || !parseDigits(value, 5, 2, month) ||
      !parseDigits(value, 8, 2, day) || !parseDigits(value, 11, 2, hour) ||
      !parseDigits(value, 14, 2, minute) ||
      !parseDigits(value, 17, 2, second)) {
    return false;
  }
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
      minute > 59 || second > 60) {
    return false;
  }
  const char* suffix = value + 19;
  if (*suffix == '.') {
    do {
      ++suffix;
    } while (*suffix >= '0' && *suffix <= '9');
  }
  if (*suffix != 'Z' || suffix[1] != '\0') return false;
  output = daysFromCivil(year, static_cast<unsigned>(month),
                         static_cast<unsigned>(day)) *
               86400LL +
           hour * 3600LL + minute * 60LL + second;
  return true;
}

GetResult getJson(const AgentEndpoint& endpoint, const char* token,
                  const char* path, size_t maximumBytes,
                  JsonStreamParser parser, void* context) {
  WiFiClient client;
  client.setTimeout(kReadTimeoutMs);
  HTTPClient http;
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kReadTimeoutMs);
  http.setReuse(false);
  http.useHTTP10(true);

  if (!http.begin(client, String(endpoint.host), endpoint.port, path, false)) {
    return {AgentPollResult::HttpBeginFailed, 0};
  }

  String authorization;
  authorization.reserve(strlen(token) + 8);
  authorization = "Bearer ";
  authorization += token;
  http.addHeader("Authorization", authorization);
  authorization = String();

  const int httpCode = http.GET();
  if (httpCode == HTTP_CODE_UNAUTHORIZED) {
    http.end();
    return {AgentPollResult::Unauthorized, httpCode};
  }
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return {AgentPollResult::HttpError, httpCode};
  }

  const int payloadSize = http.getSize();
  if (payloadSize > static_cast<int>(maximumBytes)) {
    http.end();
    return {AgentPollResult::ResponseTooLarge, httpCode};
  }

  LimitedStream input(http.getStream(), maximumBytes);
  const bool parsed = parser(input, context);
  const bool exceeded = input.exceeded();
  http.end();
  if (exceeded) return {AgentPollResult::ResponseTooLarge, httpCode};
  if (!parsed) return {AgentPollResult::InvalidJson, httpCode};
  return {AgentPollResult::Ok, httpCode};
}

struct ParsedVersion {
  char schemaVersion[16] = {};
  char installationId[48] = {};
};

bool parseVersion(Stream& input, void* context) {
  ParsedVersion& output = *static_cast<ParsedVersion*>(context);
  JsonDocument filter;
  filter["schema_version"] = true;
  filter["installation_id"] = true;
  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, input, DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(8));
  if (error || !document.is<JsonObjectConst>()) return false;
  JsonObjectConst root = document.as<JsonObjectConst>();
  const char* schema = root["schema_version"].as<const char*>();
  const char* identity = root["installation_id"].as<const char*>();
  if (schema == nullptr || identity == nullptr || schema[0] == '\0' ||
      identity[0] == '\0' || strlen(schema) >= sizeof(output.schemaVersion) ||
      strlen(identity) >= sizeof(output.installationId)) {
    return false;
  }
  strlcpy(output.schemaVersion, schema, sizeof(output.schemaVersion));
  strlcpy(output.installationId, identity, sizeof(output.installationId));
  return true;
}

struct ParsedStatus {
  app::AgentDeviceSnapshot snapshot;
  char schemaVersion[16] = {};
  char installationId[48] = {};
  int64_t generatedAt = 0;
  bool fetchThreads = false;
};

void readOptionalCount(JsonObjectConst object, const char* key,
                       int16_t& output) {
  int32_t value = -1;
  if (readNonNegative(object[key], value) && value <= INT16_MAX) {
    output = static_cast<int16_t>(value);
  }
}

bool parseStatus(Stream& input, void* context) {
  ParsedStatus& output = *static_cast<ParsedStatus*>(context);
  JsonDocument filter;
  filter["schema_version"] = true;
  filter["installation_id"] = true;
  filter["generated_at"] = true;
  filter["stale"] = true;
  filter["codex"]["visibility"] = true;
  filter["summary"]["workload_state"] = true;
  filter["summary"]["state_confidence"] = true;
  filter["summary"]["known_threads"] = true;
  filter["summary"]["active_workflows"] = true;
  filter["summary"]["active_workers"] = true;
  filter["summary"]["states"]["running"] = true;
  filter["summary"]["states"]["waiting_approval"] = true;
  filter["summary"]["states"]["waiting_input"] = true;
  filter["summary"]["states"]["error"] = true;

  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, input, DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(12));
  if (error || !document.is<JsonObjectConst>()) return false;
  JsonObjectConst root = document.as<JsonObjectConst>();
  const char* schema = root["schema_version"].as<const char*>();
  const char* identity = root["installation_id"].as<const char*>();
  const char* generated = root["generated_at"].as<const char*>();
  if (schema == nullptr || identity == nullptr || schema[0] == '\0' ||
      identity[0] == '\0' || strlen(schema) >= sizeof(output.schemaVersion) ||
      strlen(identity) >= sizeof(output.installationId) ||
      !root["stale"].is<bool>()) {
    return false;
  }
  JsonObjectConst codex = root["codex"].as<JsonObjectConst>();
  const char* visibility = codex["visibility"].as<const char*>();
  if (codex.isNull() || visibility == nullptr || visibility[0] == '\0' ||
      !parseRfc3339Utc(generated, output.generatedAt)) {
    return false;
  }
  strlcpy(output.schemaVersion, schema, sizeof(output.schemaVersion));
  strlcpy(output.installationId, identity, sizeof(output.installationId));

  JsonObjectConst summary = root["summary"].as<JsonObjectConst>();
  if (summary.isNull()) return false;
  const char* stateText = summary["workload_state"].as<const char*>();
  if (stateText == nullptr) return false;

  app::AgentDeviceSnapshot snapshot;
  snapshot.workload = parseWorkloadState(stateText);
  snapshot.stale = root["stale"].as<bool>();
  snapshot.reachability = snapshot.stale ? app::AgentReachability::Recovering
                                         : app::AgentReachability::Online;
  snapshot.identityVerified = true;
  const char* confidence = summary["state_confidence"].as<const char*>();
  snapshot.stateExact = confidence != nullptr && strcmp(confidence, "exact") == 0;
  snapshot.receivedAtMs = millis();
  snapshot.activityAgeSecondsAtReceive = UINT32_MAX;
  readOptionalCount(summary, "active_workflows", snapshot.activeWorkflows);
  readOptionalCount(summary, "active_workers", snapshot.activeWorkers);
  JsonObjectConst states = summary["states"].as<JsonObjectConst>();
  if (!states.isNull()) {
    readOptionalCount(states, "running", snapshot.running);
    readOptionalCount(states, "waiting_approval", snapshot.waitingApproval);
    readOptionalCount(states, "waiting_input", snapshot.waitingInput);
    readOptionalCount(states, "error", snapshot.errors);
  }
  int32_t knownThreads = 0;
  output.fetchThreads =
      readNonNegative(summary["known_threads"], knownThreads) &&
      knownThreads > 0 && statePriority(snapshot.workload) > 0;
  output.snapshot = snapshot;
  return true;
}

struct ParsedThreads {
  char task[app::kTaskTextSize] = {};
  uint32_t ageSeconds = UINT32_MAX;
};

bool parseThreads(Stream& input, void* context) {
  ParsedThreads& output = *static_cast<ParsedThreads*>(context);
  JsonDocument filter;
  filter["schema_version"] = true;
  filter["generated_at"] = true;
  filter["threads"][0]["state"] = true;
  filter["threads"][0]["name"] = true;
  filter["threads"][0]["preview"] = true;
  filter["threads"][0]["updated_at"] = true;

  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, input, DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(16));
  if (error || !document.is<JsonObjectConst>()) return false;
  JsonObjectConst root = document.as<JsonObjectConst>();
  const char* schema = root["schema_version"].as<const char*>();
  JsonArrayConst threads = root["threads"].as<JsonArrayConst>();
  if (!schemaMajorOne(schema) || threads.isNull()) return false;

  int selectedPriority = 0;
  char selectedUpdatedAt[40] = {};
  const char* selectedText = nullptr;
  for (JsonObjectConst thread : threads) {
    const app::WorkloadState state =
        parseWorkloadState(thread["state"].as<const char*>());
    const int priority = statePriority(state);
    if (priority == 0) continue;
    const char* updatedAt = thread["updated_at"].as<const char*>();
    if (updatedAt == nullptr) updatedAt = "";
    if (priority < selectedPriority ||
        (priority == selectedPriority &&
         strcmp(updatedAt, selectedUpdatedAt) <= 0)) {
      continue;
    }
    const char* name = thread["name"].as<const char*>();
    const char* preview = thread["preview"].as<const char*>();
    const char* candidate = name != nullptr && name[0] != '\0' ? name : preview;
    if (candidate == nullptr || candidate[0] == '\0') continue;
    selectedPriority = priority;
    selectedText = candidate;
    strlcpy(selectedUpdatedAt, updatedAt, sizeof(selectedUpdatedAt));
  }

  copyDisplayText(output.task, sizeof(output.task), selectedText);
  const char* generatedAt = root["generated_at"].as<const char*>();
  int64_t generatedEpoch = 0;
  int64_t updatedEpoch = 0;
  if (selectedUpdatedAt[0] != '\0' &&
      parseRfc3339Utc(generatedAt, generatedEpoch) &&
      parseRfc3339Utc(selectedUpdatedAt, updatedEpoch) &&
      generatedEpoch >= updatedEpoch &&
      generatedEpoch - updatedEpoch <= UINT32_MAX) {
    output.ageSeconds = static_cast<uint32_t>(generatedEpoch - updatedEpoch);
  }
  return true;
}

enum class QuotaParseState : uint8_t { Invalid, Unavailable, Available };

struct ParsedQuota {
  QuotaParseState state = QuotaParseState::Invalid;
  int16_t usedPercent = -1;
  int64_t resetTimestamp = 0;
};

bool parseWeeklyWindow(JsonObjectConst window, int16_t& usedPercent,
                       int64_t& resetTimestamp) {
  if (window.isNull()) return false;
  int32_t duration = 0;
  if (!readNonNegative(window["windowDurationMins"], duration) ||
      duration < kWeeklyWindowMinimumMinutes ||
      duration > kWeeklyWindowMaximumMinutes) {
    return false;
  }
  JsonVariantConst used = window["usedPercent"];
  if (!used.is<double>() && !used.is<int64_t>() && !used.is<uint64_t>()) {
    return false;
  }
  const double parsedPercent = used.as<double>();
  if (!isfinite(parsedPercent) || parsedPercent < 0.0 ||
      parsedPercent > 100.0) {
    return false;
  }
  int64_t parsedReset = 0;
  if (!readEpochSeconds(window["resetsAt"], parsedReset)) return false;
  usedPercent = static_cast<int16_t>(lround(parsedPercent));
  resetTimestamp = parsedReset;
  return true;
}

bool parseRateLimits(Stream& input, void* context) {
  ParsedQuota& output = *static_cast<ParsedQuota*>(context);
  JsonDocument filter;
  filter["availability"] = true;
  filter["rateLimits"]["primary"]["usedPercent"] = true;
  filter["rateLimits"]["primary"]["windowDurationMins"] = true;
  filter["rateLimits"]["primary"]["resetsAt"] = true;
  filter["rateLimits"]["secondary"]["usedPercent"] = true;
  filter["rateLimits"]["secondary"]["windowDurationMins"] = true;
  filter["rateLimits"]["secondary"]["resetsAt"] = true;

  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, input, DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(10));
  if (error || !document.is<JsonObjectConst>()) return false;
  JsonObjectConst root = document.as<JsonObjectConst>();
  const char* availability = root["availability"].as<const char*>();
  if (availability == nullptr) return false;
  if (strcmp(availability, "unavailable") == 0) {
    output.state = QuotaParseState::Unavailable;
    return true;
  }
  if (strcmp(availability, "available") != 0) return false;

  JsonObjectConst limits = root["rateLimits"].as<JsonObjectConst>();
  if (limits.isNull()) return false;
  int16_t usedPercent = -1;
  int64_t resetTimestamp = 0;
  if (!parseWeeklyWindow(limits["primary"].as<JsonObjectConst>(), usedPercent,
                         resetTimestamp) &&
      !parseWeeklyWindow(limits["secondary"].as<JsonObjectConst>(),
                         usedPercent, resetTimestamp)) {
    return false;
  }
  output.state = QuotaParseState::Available;
  output.usedPercent = usedPercent;
  output.resetTimestamp = resetTimestamp;
  return true;
}

uint8_t configuredMask(const AgentFleetSettings& settings) {
  uint8_t mask = 0;
  for (size_t i = 0; i < app::kAgentDeviceCount; ++i) {
    if (settings.endpoints[i].configured()) {
      mask |= static_cast<uint8_t>(1U << i);
    }
  }
  return mask;
}

}  // namespace

const char* agentPollResultName(AgentPollResult result) {
  switch (result) {
    case AgentPollResult::Ok:
      return "ok";
    case AgentPollResult::WifiDisconnected:
      return "wifi_disconnected";
    case AgentPollResult::SettingsMissing:
      return "settings_missing";
    case AgentPollResult::SettingsPersistFailed:
      return "settings_persist_failed";
    case AgentPollResult::HttpBeginFailed:
      return "http_begin_failed";
    case AgentPollResult::HttpError:
      return "http_error";
    case AgentPollResult::Unauthorized:
      return "unauthorized";
    case AgentPollResult::ResponseTooLarge:
      return "response_too_large";
    case AgentPollResult::InvalidJson:
      return "invalid_json";
    case AgentPollResult::IncompatibleSchema:
      return "incompatible_schema";
    case AgentPollResult::IdentityMismatch:
      return "identity_mismatch";
    case AgentPollResult::Superseded:
      return "superseded";
    case AgentPollResult::NotStarted:
    default:
      return "not_started";
  }
}

AgentClient::AgentClient(app::RuntimeState& state, SettingsStore& settings,
                         uint32_t interDevicePollMs)
    : state_(state),
      settings_(settings),
      interDevicePollMs_(interDevicePollMs < 1000 ? 1000
                                                  : interDevicePollMs) {}

AgentClient::~AgentClient() { end(); }

bool AgentClient::begin() {
  if (taskHandle_ != nullptr) return true;
  stopRequested_ = false;
  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "ep75-agents", kAgentTaskStackBytes, this,
      kAgentTaskPriority, &taskHandle_, kAgentTaskCore);
  if (created != pdPASS) {
    taskHandle_ = nullptr;
    return false;
  }
  return true;
}

void AgentClient::end() {
  TaskHandle_t task = taskHandle_;
  if (task == nullptr) return;
  stopRequested_ = true;
  const uint32_t startedAt = millis();
  while (taskHandle_ != nullptr && millis() - startedAt < 1500) delay(10);
  if (taskHandle_ != nullptr) {
    vTaskDelete(task);
    taskHandle_ = nullptr;
    running_ = false;
  }
  secureZero(activeSettings_.token, sizeof(activeSettings_.token));
}

void AgentClient::requestImmediatePoll() {
  portENTER_CRITICAL(&mux_);
  immediatePoll_ = true;
  portEXIT_CRITICAL(&mux_);
}

bool AgentClient::running() const { return running_; }

AgentPollResult AgentClient::lastResult() const {
  portENTER_CRITICAL(&mux_);
  const AgentPollResult result = lastResult_;
  portEXIT_CRITICAL(&mux_);
  return result;
}

int AgentClient::lastHttpCode() const {
  portENTER_CRITICAL(&mux_);
  const int value = lastHttpCode_;
  portEXIT_CRITICAL(&mux_);
  return value;
}

uint32_t AgentClient::lastDurationMs() const {
  portENTER_CRITICAL(&mux_);
  const uint32_t value = lastDurationMs_;
  portEXIT_CRITICAL(&mux_);
  return value;
}

void AgentClient::taskEntry(void* context) {
  static_cast<AgentClient*>(context)->run();
}

void AgentClient::run() {
  running_ = true;
  reloadSettings(true);
  while (!stopRequested_) {
    const uint32_t nowMs = millis();
    if (lastSettingsCheckAtMs_ == 0 ||
        nowMs - lastSettingsCheckAtMs_ >= kSettingsCheckPeriodMs) {
      reloadSettings(false);
      lastSettingsCheckAtMs_ = nowMs;
    }

    portENTER_CRITICAL(&mux_);
    const bool immediate = immediatePoll_;
    immediatePoll_ = false;
    portEXIT_CRITICAL(&mux_);
    if (immediate) {
      for (SlotRuntime& slot : slots_) slot.nextAttemptAtMs = 0;
      nextSlotIndex_ = activeSettings_.primaryIndex < app::kAgentDeviceCount
                           ? activeSettings_.primaryIndex
                           : 0;
      nextPollAtMs_ = nowMs;
      quotaCycleActive_ = true;
      quotaCycleStartedAtMs_ = nowMs;
      quotaAttemptMask_ = 0;
    }

    if (WiFi.status() != WL_CONNECTED) {
      if (!wifiOfflinePublished_) publishWifiUnavailable();
      recordResult(AgentPollResult::WifiDisconnected, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (wifiOfflinePublished_) {
      wifiOfflinePublished_ = false;
      requestImmediatePoll();
    }

    if (quotaCycleActive_ &&
        nowMs - quotaCycleStartedAtMs_ >= kQuotaCycleTimeoutMs) {
      finishQuotaCycleUnavailable(nowMs);
    }

    if (static_cast<int32_t>(nowMs - nextPollAtMs_) >= 0) {
      pollSlot(nextSlotIndex_);
      maybePollQuota(nextSlotIndex_, millis());
      nextSlotIndex_ = (nextSlotIndex_ + 1U) % app::kAgentDeviceCount;
      nextPollAtMs_ = millis() + interDevicePollMs_;
    }
    vTaskDelay(pdMS_TO_TICKS(25));
  }

  secureZero(activeSettings_.token, sizeof(activeSettings_.token));
  running_ = false;
  taskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

bool AgentClient::reloadSettings(bool force) {
  StoredSettings stored;
  if (!settings_.load(stored)) return false;
  if (!force && hasSettings_ && sameSettings(activeSettings_, stored.agents)) {
    secureZero(stored.agents.token, sizeof(stored.agents.token));
    return true;
  }
  resetForSettings(stored.agents);
  secureZero(stored.agents.token, sizeof(stored.agents.token));
  requestImmediatePoll();
  return true;
}

void AgentClient::resetForSettings(const AgentFleetSettings& settings) {
  secureZero(activeSettings_.token, sizeof(activeSettings_.token));
  activeSettings_ = settings;
  hasSettings_ = true;
  wifiOfflinePublished_ = false;
  for (size_t i = 0; i < app::kAgentDeviceCount; ++i) {
    slots_[i] = SlotRuntime();
    app::copyText(slots_[i].snapshot.label, kDeviceLabels[i]);
    strlcpy(slots_[i].installationId, activeSettings_.installationIds[i],
            sizeof(slots_[i].installationId));
    if (!activeSettings_.endpoints[i].configured()) {
      slots_[i].snapshot.reachability =
          app::AgentReachability::Unconfigured;
    } else if (!activeSettings_.hasToken()) {
      slots_[i].snapshot.reachability = app::AgentReachability::AuthError;
      slots_[i].authLocked = true;
    } else {
      slots_[i].snapshot.reachability = app::AgentReachability::Connecting;
    }
  }
  account_ = app::AccountQuotaSnapshot();
  account_.sourceIndex = activeSettings_.primaryIndex;
  nextSlotIndex_ = activeSettings_.primaryIndex < app::kAgentDeviceCount
                       ? activeSettings_.primaryIndex
                       : 0;
  nextQuotaAtMs_ = millis();
  quotaCycleActive_ = true;
  quotaCycleStartedAtMs_ = millis();
  quotaAttemptMask_ = 0;
  publishFleet();
}

bool AgentClient::pollSlot(size_t slotIndex) {
  if (slotIndex >= app::kAgentDeviceCount) return false;
  const uint32_t startedAtMs = millis();
  SlotRuntime& slot = slots_[slotIndex];
  const AgentEndpoint& endpoint = activeSettings_.endpoints[slotIndex];
  if (!endpoint.configured() || !activeSettings_.hasToken()) {
    slot.snapshot.reachability = endpoint.configured()
                                        ? app::AgentReachability::AuthError
                                        : app::AgentReachability::Unconfigured;
    slot.snapshot.stale = true;
    publishFleet();
    recordResult(AgentPollResult::SettingsMissing, 0,
                 millis() - startedAtMs);
    return false;
  }
  if (slot.authLocked || slot.protocolLocked) return false;
  if (slot.nextAttemptAtMs != 0 &&
      static_cast<int32_t>(startedAtMs - slot.nextAttemptAtMs) < 0) {
    return false;
  }

  if (!slot.identityVerified) {
    ParsedVersion version;
    const GetResult versionResult =
        getJson(endpoint, activeSettings_.token, "/api/v1/version",
                kVersionPayloadLimit, parseVersion, &version);
    if (versionResult.result != AgentPollResult::Ok) {
      publishFailure(slotIndex, versionResult.result,
                     versionResult.httpCode, startedAtMs);
      return false;
    }
    if (!schemaMajorOne(version.schemaVersion)) {
      publishFailure(slotIndex, AgentPollResult::IncompatibleSchema,
                     versionResult.httpCode, startedAtMs);
      return false;
    }
    if (slot.installationId[0] != '\0' &&
        strcmp(slot.installationId, version.installationId) != 0) {
      publishFailure(slotIndex, AgentPollResult::IdentityMismatch,
                     versionResult.httpCode, startedAtMs);
      return false;
    }
    if (slot.installationId[0] == '\0') {
      AgentFleetSettings pinned;
      const PinInstallationIdResult pinResult = settings_.pinInstallationId(
          slotIndex, activeSettings_, version.installationId, pinned);
      if (pinResult == PinInstallationIdResult::Superseded) {
        secureZero(pinned.token, sizeof(pinned.token));
        reloadSettings(true);
        recordResult(AgentPollResult::Superseded, versionResult.httpCode,
                     millis() - startedAtMs);
        return false;
      }
      if (pinResult != PinInstallationIdResult::Pinned &&
          pinResult != PinInstallationIdResult::AlreadyPinned) {
        secureZero(pinned.token, sizeof(pinned.token));
        publishFailure(slotIndex, AgentPollResult::SettingsPersistFailed,
                       versionResult.httpCode, startedAtMs);
        return false;
      }
      activeSettings_ = pinned;
      secureZero(pinned.token, sizeof(pinned.token));
    }
    strlcpy(slot.installationId, version.installationId,
            sizeof(slot.installationId));
    slot.identityVerified = true;
  }

  ParsedStatus status;
  const GetResult statusResult =
      getJson(endpoint, activeSettings_.token, "/api/v1/status",
              kStatusPayloadLimit, parseStatus, &status);
  if (statusResult.result != AgentPollResult::Ok) {
    publishFailure(slotIndex, statusResult.result, statusResult.httpCode,
                   startedAtMs);
    return false;
  }
  if (!schemaMajorOne(status.schemaVersion)) {
    publishFailure(slotIndex, AgentPollResult::IncompatibleSchema,
                   statusResult.httpCode, startedAtMs);
    return false;
  }
  if (strcmp(slot.installationId, status.installationId) != 0) {
    publishFailure(slotIndex, AgentPollResult::IdentityMismatch,
                   statusResult.httpCode, startedAtMs);
    return false;
  }

  app::copyText(status.snapshot.label, kDeviceLabels[slotIndex]);
  if (status.fetchThreads) {
    ParsedThreads threads;
    const GetResult threadResult = getJson(
        endpoint, activeSettings_.token, "/api/v1/threads?limit=12",
        kThreadsPayloadLimit, parseThreads, &threads);
    if (threadResult.result == AgentPollResult::Unauthorized) {
      publishFailure(slotIndex, threadResult.result, threadResult.httpCode,
                     startedAtMs);
      return false;
    }
    if (threadResult.result == AgentPollResult::Ok) {
      app::copyText(status.snapshot.currentTask, threads.task);
      status.snapshot.activityAgeSecondsAtReceive = threads.ageSeconds;
    } else {
      app::copyText(status.snapshot.currentTask, "Task details unavailable");
      status.snapshot.activityAgeSecondsAtReceive = UINT32_MAX;
    }
  }

  slot.snapshot = status.snapshot;
  slot.lastGeneratedTimestamp = status.generatedAt;
  slot.identityVerified = true;
  slot.consecutiveFailures = 0;
  slot.nextAttemptAtMs = 0;
  publishFleet();
  recordResult(AgentPollResult::Ok, statusResult.httpCode,
               millis() - startedAtMs);
  return true;
}

void AgentClient::publishFleet() {
  app::AgentFleetSnapshot fleet;
  fleet.account = account_;
  for (size_t i = 0; i < app::kAgentDeviceCount; ++i) {
    fleet.devices[i] = slots_[i].snapshot;
  }
  state_.updateFleet(fleet);
}

void AgentClient::publishWifiUnavailable() {
  wifiOfflinePublished_ = true;
  for (size_t i = 0; i < app::kAgentDeviceCount; ++i) {
    if (!activeSettings_.endpoints[i].configured()) {
      slots_[i].snapshot.reachability =
          app::AgentReachability::Unconfigured;
      continue;
    }
    // Authentication and identity/schema locks survive a Wi-Fi outage and
    // can only be cleared by replacing the private USB-provisioned settings.
    if (slots_[i].authLocked) {
      slots_[i].snapshot.reachability = app::AgentReachability::AuthError;
      continue;
    }
    if (slots_[i].protocolLocked) {
      slots_[i].snapshot.reachability = app::AgentReachability::ProtocolError;
      continue;
    }
    slots_[i].snapshot.reachability = app::AgentReachability::Offline;
    slots_[i].snapshot.stale = true;
  }
  if (account_.weeklyUsedPercent >= 0) {
    account_.stale = true;
  } else {
    account_.availability = app::DataAvailability::Unavailable;
    account_.stale = true;
  }
  publishFleet();
}

void AgentClient::publishFailure(size_t slotIndex, AgentPollResult result,
                                 int httpCode, uint32_t startedAtMs) {
  SlotRuntime& slot = slots_[slotIndex];
  slot.snapshot.stale = true;
  if (result == AgentPollResult::Unauthorized) {
    slot.authLocked = true;
    slot.identityVerified = false;
    slot.snapshot.identityVerified = false;
    slot.snapshot.reachability = app::AgentReachability::AuthError;
  } else if (result == AgentPollResult::IncompatibleSchema ||
             result == AgentPollResult::IdentityMismatch) {
    slot.protocolLocked = true;
    slot.identityVerified = false;
    slot.snapshot.identityVerified = false;
    slot.snapshot.reachability = app::AgentReachability::ProtocolError;
  } else {
    if (slot.consecutiveFailures < UINT8_MAX) ++slot.consecutiveFailures;
    slot.snapshot.reachability =
        slot.consecutiveFailures >= kFailuresBeforeOffline
            ? app::AgentReachability::Offline
            : app::AgentReachability::Recovering;
    if (slot.consecutiveFailures >= kFailuresBeforeOffline) {
      uint8_t shift = slot.consecutiveFailures - kFailuresBeforeOffline;
      if (shift > 4) shift = 4;
      uint32_t delayMs = kOfflineBackoffBaseMs << shift;
      if (delayMs > kOfflineBackoffMaximumMs) {
        delayMs = kOfflineBackoffMaximumMs;
      }
      slot.nextAttemptAtMs = millis() + delayMs;
    }
  }
  publishFleet();
  recordResult(result, httpCode, millis() - startedAtMs);
}

void AgentClient::maybePollQuota(size_t slotIndex, uint32_t nowMs) {
  if (!quotaCycleActive_ &&
      static_cast<int32_t>(nowMs - nextQuotaAtMs_) >= 0) {
    quotaCycleActive_ = true;
    quotaCycleStartedAtMs_ = nowMs;
    quotaAttemptMask_ = 0;
  }
  if (!quotaCycleActive_) return;

  const size_t primary = activeSettings_.primaryIndex < app::kAgentDeviceCount
                             ? activeSettings_.primaryIndex
                             : 0;
  const uint8_t slotBit = static_cast<uint8_t>(1U << slotIndex);
  const uint8_t primaryBit = static_cast<uint8_t>(1U << primary);
  if ((quotaAttemptMask_ & slotBit) != 0) return;
  if ((quotaAttemptMask_ & primaryBit) == 0 && slotIndex != primary) return;

  quotaAttemptMask_ |= slotBit;
  const SlotRuntime& slot = slots_[slotIndex];
  const bool canSupplyQuota =
      slot.identityVerified && !slot.authLocked && !slot.protocolLocked &&
      slot.snapshot.reachability == app::AgentReachability::Online;
  const uint8_t availableSlots = configuredMask(activeSettings_);
  if (!canSupplyQuota) {
    if ((quotaAttemptMask_ & availableSlots) == availableSlots) {
      finishQuotaCycleUnavailable(nowMs);
    }
    return;
  }

  ParsedQuota quota;
  const AgentEndpoint& endpoint = activeSettings_.endpoints[slotIndex];
  const GetResult result = getJson(
      endpoint, activeSettings_.token, "/api/v1/rate-limits",
      kRateLimitsPayloadLimit, parseRateLimits, &quota);
  if (result.result == AgentPollResult::Unauthorized) {
    publishFailure(slotIndex, result.result, result.httpCode, nowMs);
  }
  if (result.result == AgentPollResult::Ok &&
      quota.state == QuotaParseState::Available) {
    account_.availability = app::DataAvailability::Available;
    account_.weeklyUsedPercent = quota.usedPercent;
    account_.weeklyResetTimestamp = quota.resetTimestamp;
    account_.sourceGeneratedTimestamp = slots_[slotIndex].lastGeneratedTimestamp;
    account_.receivedAtMs = millis();
    account_.sourceIndex = static_cast<uint8_t>(slotIndex);
    account_.stale = false;
    quotaCycleActive_ = false;
    quotaAttemptMask_ = 0;
    nextQuotaAtMs_ = millis() + kQuotaPollPeriodMs;
    publishFleet();
    return;
  }

  if ((quotaAttemptMask_ & availableSlots) == availableSlots) {
    finishQuotaCycleUnavailable(millis());
  }
}

void AgentClient::finishQuotaCycleUnavailable(uint32_t nowMs) {
  account_.availability = app::DataAvailability::Unavailable;
  account_.stale = true;
  quotaCycleActive_ = false;
  quotaAttemptMask_ = 0;
  nextQuotaAtMs_ = nowMs + kQuotaPollPeriodMs;
  publishFleet();
}

void AgentClient::recordResult(AgentPollResult result, int httpCode,
                               uint32_t durationMs) {
  portENTER_CRITICAL(&mux_);
  lastResult_ = result;
  lastHttpCode_ = httpCode;
  lastDurationMs_ = durationMs;
  portEXIT_CRITICAL(&mux_);
}

}  // namespace net
