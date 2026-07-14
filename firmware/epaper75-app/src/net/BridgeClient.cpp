#include "net/BridgeClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <limits.h>
#include <math.h>
#include <string.h>

namespace net {
namespace {

// Keep the failed local-LAN path bounded to about three seconds while allowing
// the desktop Bridge enough time for an occasional log-cache refresh.
constexpr uint16_t kConnectTimeoutMs = 1000;
constexpr uint16_t kReadTimeoutMs = 2000;
constexpr uint8_t kFailuresBeforeOffline = 3;
constexpr int kMaximumPayloadBytes = 16 * 1024;
constexpr int32_t kClaudeFiveHourMinutes = 5 * 60;
constexpr int32_t kSevenDayMinutes = 7 * 24 * 60;
constexpr int64_t kResetTimestampStabilizationSeconds = 3 * 60;

int64_t resetTimestampFromMinutes(int64_t sourceTimestamp,
                                  int32_t resetMinutes) {
  if (sourceTimestamp <= 0 || resetMinutes < 0) return 0;
  return sourceTimestamp + static_cast<int64_t>(resetMinutes) * 60LL;
}

void stabilizeResetTimestamp(int64_t& candidate, int64_t previous) {
  if (candidate <= 0 || previous <= 0) return;
  const int64_t delta = candidate > previous ? candidate - previous
                                             : previous - candidate;
  // The Bridge refreshes quota data less often than /status. Reusing its
  // minute countdown with a newer `ts` otherwise makes the apparent absolute
  // reset time creep forward between quota fetches. Keep the established
  // timestamp when the difference is only protocol/cache quantization.
  if (delta <= kResetTimestampStabilizationSeconds) candidate = previous;
}

bool hasKey(JsonObjectConst object, const char* key) {
  // isNull() cannot distinguish a missing member from an explicit JSON null;
  // quota fields allow null but still require the documented snake_case key.
  return object.containsKey(key);
}

bool readString(JsonObjectConst object, const char* key, char* output,
                size_t outputSize) {
  if (!hasKey(object, key)) return false;
  JsonVariantConst value = object[key];
  if (!value.is<const char*>()) return false;
  const char* text = value.as<const char*>();
  const size_t length = text == nullptr ? 0 : strnlen(text, outputSize);
  if (length == 0 || length >= outputSize) return false;
  strlcpy(output, text, outputSize);
  return true;
}

bool readBool(JsonObjectConst object, const char* key, bool& output) {
  if (!hasKey(object, key)) return false;
  JsonVariantConst value = object[key];
  if (!value.is<bool>()) return false;
  output = value.as<bool>();
  return true;
}

bool readInteger(JsonVariantConst value, int64_t minimum, int64_t maximum,
                 int64_t& output) {
  if (!value.is<int64_t>() && !value.is<uint64_t>()) return false;
  if (value.is<uint64_t>() && value.as<uint64_t>() >
                                  static_cast<uint64_t>(INT64_MAX)) {
    return false;
  }
  const int64_t parsed = value.as<int64_t>();
  if (parsed < minimum || parsed > maximum) return false;
  output = parsed;
  return true;
}

bool readInt32(JsonObjectConst object, const char* key, int32_t minimum,
               int32_t maximum, int32_t& output) {
  if (!hasKey(object, key)) return false;
  int64_t parsed = 0;
  if (!readInteger(object[key], minimum, maximum, parsed)) return false;
  output = static_cast<int32_t>(parsed);
  return true;
}

bool readNullableInt32(JsonObjectConst object, const char* key,
                       int32_t& output) {
  if (!hasKey(object, key)) return false;
  JsonVariantConst value = object[key];
  if (value.isNull()) {
    output = -1;
    return true;
  }
  int64_t parsed = 0;
  if (!readInteger(value, 0, INT32_MAX, parsed)) return false;
  output = static_cast<int32_t>(parsed);
  return true;
}

bool readNullablePercent(JsonObjectConst object, const char* key,
                         int16_t& output) {
  if (!hasKey(object, key)) return false;
  JsonVariantConst value = object[key];
  if (value.isNull()) {
    output = -1;
    return true;
  }
  // Utf8JsonWriter may serialize a mathematically integral double without a
  // decimal point. ArduinoJson then stores it as an integer, which is still a
  // valid JSON number for this contract.
  if (!value.is<double>() && !value.is<int64_t>() &&
      !value.is<uint64_t>()) {
    return false;
  }
  const double parsed = value.as<double>();
  if (!isfinite(parsed) || parsed < 0.0 || parsed > 100.0) return false;
  output = static_cast<int16_t>(lround(parsed));
  return true;
}

bool parseClaude(JsonObjectConst object, app::AgentSnapshot& output) {
  return readString(object, "status", output.status, sizeof(output.status)) &&
         readInt32(object, "tokens_today", 0, INT32_MAX,
                   output.tokensToday) &&
         readInt32(object, "session_min", 0, INT32_MAX,
                   output.sessionMinutes) &&
         readInt32(object, "session_window_min", 1, INT32_MAX,
                   output.sessionWindowMinutes) &&
         readNullablePercent(object, "five_hour_pct",
                             output.primaryPercent) &&
         readNullableInt32(object, "five_hour_reset_min",
                           output.primaryResetMinutes) &&
         readNullablePercent(object, "seven_day_pct",
                             output.secondaryPercent) &&
         readNullableInt32(object, "seven_day_reset_min",
                           output.secondaryResetMinutes) &&
         readBool(object, "needs_input", output.needsInput);
}

bool parseCodex(JsonObjectConst object, app::AgentSnapshot& output) {
  return readString(object, "status", output.status, sizeof(output.status)) &&
         readInt32(object, "tokens_today", 0, INT32_MAX,
                   output.tokensToday) &&
         readNullablePercent(object, "primary_pct", output.primaryPercent) &&
         readNullableInt32(object, "primary_window_min",
                           output.primaryWindowMinutes) &&
         readNullableInt32(object, "primary_reset_min",
                           output.primaryResetMinutes) &&
         readNullablePercent(object, "weekly_pct", output.secondaryPercent) &&
         readNullableInt32(object, "weekly_window_min",
                           output.secondaryWindowMinutes) &&
         readNullableInt32(object, "weekly_reset_min",
                           output.secondaryResetMinutes) &&
         readBool(object, "needs_input", output.needsInput);
}

bool parseStatus(Stream& input, app::BridgeSnapshot& output) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, input);
  if (error || !document.is<JsonObject>()) return false;

  JsonObjectConst root = document.as<JsonObjectConst>();
  JsonVariantConst claudeValue = root["claude"];
  JsonVariantConst codexValue = root["codex"];
  if (!claudeValue.is<JsonObjectConst>() || !codexValue.is<JsonObjectConst>() ||
      !hasKey(root, "ts")) {
    return false;
  }

  int64_t sourceTimestamp = 0;
  if (!readInteger(root["ts"], 0, INT64_MAX, sourceTimestamp)) return false;

  app::BridgeSnapshot parsed;
  if (!parseClaude(claudeValue.as<JsonObjectConst>(), parsed.claude) ||
      !parseCodex(codexValue.as<JsonObjectConst>(), parsed.codex)) {
    return false;
  }

  parsed.claude.primaryWindowMinutes = kClaudeFiveHourMinutes;
  parsed.claude.secondaryWindowMinutes = kSevenDayMinutes;
  parsed.sourceTimestamp = sourceTimestamp;
  parsed.claude.primaryResetTimestamp = resetTimestampFromMinutes(
      sourceTimestamp, parsed.claude.primaryResetMinutes);
  parsed.claude.secondaryResetTimestamp = resetTimestampFromMinutes(
      sourceTimestamp, parsed.claude.secondaryResetMinutes);
  parsed.codex.primaryResetTimestamp = resetTimestampFromMinutes(
      sourceTimestamp, parsed.codex.primaryResetMinutes);
  parsed.codex.secondaryResetTimestamp = resetTimestampFromMinutes(
      sourceTimestamp, parsed.codex.secondaryResetMinutes);
  parsed.receivedAtMs = millis();
  parsed.reachable = true;
  output = parsed;
  return true;
}

}  // namespace

const char* bridgePollResultName(BridgePollResult result) {
  switch (result) {
    case BridgePollResult::Ok:
      return "ok";
    case BridgePollResult::WifiDisconnected:
      return "wifi_disconnected";
    case BridgePollResult::EndpointMissing:
      return "endpoint_missing";
    case BridgePollResult::HttpBeginFailed:
      return "http_begin_failed";
    case BridgePollResult::HttpError:
      return "http_error";
    case BridgePollResult::ResponseTooLarge:
      return "response_too_large";
    case BridgePollResult::InvalidJson:
      return "invalid_json";
    case BridgePollResult::Superseded:
      return "superseded";
    case BridgePollResult::NotStarted:
    default:
      return "not_started";
  }
}

BridgeClient::BridgeClient(app::RuntimeState& state, uint32_t pollIntervalMs)
    : state_(state),
      pollIntervalMs_(pollIntervalMs < 1000 ? 1000 : pollIntervalMs) {}

void BridgeClient::begin() {
  portENTER_CRITICAL(&mux_);
  begun_ = true;
  immediatePoll_ = true;
  nextPollAtMs_ = millis();
  portEXIT_CRITICAL(&mux_);
}

void BridgeClient::loop() {
  if (!begun_) return;
  const uint32_t now = millis();
  const app::BridgeEndpoint endpoint = state_.bridgeEndpoint();

  portENTER_CRITICAL(&mux_);
  const bool requested = immediatePoll_;
  immediatePoll_ = false;
  const uint32_t dueAt = nextPollAtMs_;
  portEXIT_CRITICAL(&mux_);

  if (!requested && !endpointChanged(endpoint) &&
      static_cast<int32_t>(now - dueAt) < 0) {
    return;
  }

  rememberEndpoint(endpoint);
  pollNow();
  portENTER_CRITICAL(&mux_);
  nextPollAtMs_ = millis() + pollIntervalMs_;
  portEXIT_CRITICAL(&mux_);
}

bool BridgeClient::pollNow() {
  if (!begun_) begin();
  const uint32_t startedAt = millis();
  const app::BridgeEndpoint endpoint = state_.bridgeEndpoint();

  if (WiFi.status() != WL_CONNECTED) {
    publishUnavailable(BridgePollResult::WifiDisconnected, 0, startedAt);
    return false;
  }
  if (!endpoint.configured()) {
    publishUnavailable(BridgePollResult::EndpointMissing, 0, startedAt);
    return false;
  }

  WiFiClient client;
  client.setTimeout(kReadTimeoutMs);
  HTTPClient http;
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kReadTimeoutMs);
  http.setReuse(false);
  http.useHTTP10(true);

  if (!http.begin(client, String(endpoint.host), endpoint.port, "/status",
                  false)) {
    publishUnavailable(BridgePollResult::HttpBeginFailed, 0, startedAt);
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    publishUnavailable(BridgePollResult::HttpError, httpCode, startedAt);
    return false;
  }

  const int payloadSize = http.getSize();
  if (payloadSize > kMaximumPayloadBytes) {
    http.end();
    publishUnavailable(BridgePollResult::ResponseTooLarge, httpCode, startedAt);
    return false;
  }

  app::BridgeSnapshot snapshot;
  const bool parsed = parseStatus(http.getStream(), snapshot);
  http.end();
  if (!parsed) {
    publishUnavailable(BridgePollResult::InvalidJson, httpCode, startedAt);
    return false;
  }

  const app::BridgeSnapshot previous = state_.snapshot().bridge;
  stabilizeResetTimestamp(snapshot.codex.primaryResetTimestamp,
                          previous.codex.primaryResetTimestamp);
  stabilizeResetTimestamp(snapshot.codex.secondaryResetTimestamp,
                          previous.codex.secondaryResetTimestamp);

  // A POST /api/bridge can replace the endpoint on core 0 while this request
  // is in flight. Never publish a late response from the previous Bridge.
  const app::BridgeEndpoint currentEndpoint = state_.bridgeEndpoint();
  if (currentEndpoint.port != endpoint.port ||
      strcmp(currentEndpoint.host, endpoint.host) != 0) {
    requestImmediatePoll();
    publishUnavailable(BridgePollResult::Superseded, httpCode, startedAt);
    return false;
  }

  portENTER_CRITICAL(&mux_);
  consecutiveFailures_ = 0;
  portEXIT_CRITICAL(&mux_);
  state_.updateBridge(snapshot);
  recordResult(BridgePollResult::Ok, httpCode, millis() - startedAt);
  return true;
}

void BridgeClient::requestImmediatePoll() {
  portENTER_CRITICAL(&mux_);
  immediatePoll_ = true;
  portEXIT_CRITICAL(&mux_);
}

bool BridgeClient::endpointChanged(
    const app::BridgeEndpoint& endpoint) const {
  portENTER_CRITICAL(&mux_);
  const bool changed = !hasLastEndpoint_ ||
                       lastEndpoint_.port != endpoint.port ||
                       strcmp(lastEndpoint_.host, endpoint.host) != 0;
  portEXIT_CRITICAL(&mux_);
  return changed;
}

void BridgeClient::rememberEndpoint(const app::BridgeEndpoint& endpoint) {
  portENTER_CRITICAL(&mux_);
  lastEndpoint_ = endpoint;
  hasLastEndpoint_ = true;
  portEXIT_CRITICAL(&mux_);
}

void BridgeClient::publishUnavailable(BridgePollResult result, int httpCode,
                                      uint32_t startedAtMs) {
  const bool immediate = result == BridgePollResult::WifiDisconnected ||
                         result == BridgePollResult::EndpointMissing ||
                         result == BridgePollResult::Superseded;
  portENTER_CRITICAL(&mux_);
  if (immediate) {
    consecutiveFailures_ = kFailuresBeforeOffline;
  } else if (consecutiveFailures_ < UINT8_MAX) {
    ++consecutiveFailures_;
  }
  const bool publishOffline =
      consecutiveFailures_ >= kFailuresBeforeOffline;
  portEXIT_CRITICAL(&mux_);

  if (publishOffline) {
    app::BridgeSnapshot unavailable = state_.snapshot().bridge;
    unavailable.reachable = false;
    state_.updateBridge(unavailable);
  }
  recordResult(result, httpCode, millis() - startedAtMs);
}

void BridgeClient::recordResult(BridgePollResult result, int httpCode,
                                uint32_t durationMs) {
  portENTER_CRITICAL(&mux_);
  lastResult_ = result;
  lastHttpCode_ = httpCode;
  lastDurationMs_ = durationMs;
  portEXIT_CRITICAL(&mux_);
}

BridgePollResult BridgeClient::lastResult() const {
  portENTER_CRITICAL(&mux_);
  const BridgePollResult result = lastResult_;
  portEXIT_CRITICAL(&mux_);
  return result;
}

int BridgeClient::lastHttpCode() const {
  portENTER_CRITICAL(&mux_);
  const int code = lastHttpCode_;
  portEXIT_CRITICAL(&mux_);
  return code;
}

uint32_t BridgeClient::lastDurationMs() const {
  portENTER_CRITICAL(&mux_);
  const uint32_t duration = lastDurationMs_;
  portEXIT_CRITICAL(&mux_);
  return duration;
}

}  // namespace net
