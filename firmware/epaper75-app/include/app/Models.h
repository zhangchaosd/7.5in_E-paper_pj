#pragma once
#ifndef EPAPER75_APP_MODELS_H_
#define EPAPER75_APP_MODELS_H_

#include <Arduino.h>
#include <math.h>
#include <string.h>

namespace app {

constexpr size_t kStatusTextSize = 20;
constexpr size_t kHostTextSize = 64;
constexpr size_t kSsidTextSize = 33;
constexpr size_t kIpTextSize = 16;
constexpr size_t kLatencyTargetCount = 4;
constexpr size_t kLatencyNameSize = 12;

enum class DisplayMode : uint8_t { Auto = 0, Claude = 1, Codex = 2 };

inline const char* displayModeName(DisplayMode mode) {
  switch (mode) {
    case DisplayMode::Claude:
      return "claude";
    case DisplayMode::Codex:
      return "codex";
    case DisplayMode::Auto:
    default:
      return "auto";
  }
}

inline bool parseDisplayMode(const char* value, DisplayMode& mode) {
  if (value == nullptr) return false;
  if (strcasecmp(value, "auto") == 0) mode = DisplayMode::Auto;
  else if (strcasecmp(value, "claude") == 0) mode = DisplayMode::Claude;
  else if (strcasecmp(value, "codex") == 0) mode = DisplayMode::Codex;
  else return false;
  return true;
}

template <size_t N>
inline void copyText(char (&destination)[N], const char* source) {
  if (source == nullptr) source = "";
  strlcpy(destination, source, N);
}

struct BridgeEndpoint {
  char host[kHostTextSize] = {};
  uint16_t port = 8765;

  bool configured() const { return host[0] != '\0'; }
};

struct AgentSnapshot {
  char status[kStatusTextSize] = "offline";
  int32_t tokensToday = 0;
  int32_t sessionMinutes = -1;
  int32_t sessionWindowMinutes = -1;
  int16_t primaryPercent = -1;
  int32_t primaryWindowMinutes = -1;
  int32_t primaryResetMinutes = -1;
  int64_t primaryResetTimestamp = 0;
  int16_t secondaryPercent = -1;
  int32_t secondaryWindowMinutes = -1;
  int32_t secondaryResetMinutes = -1;
  int64_t secondaryResetTimestamp = 0;
  bool needsInput = false;
};

struct BridgeSnapshot {
  bool reachable = false;
  int64_t sourceTimestamp = 0;
  uint32_t receivedAtMs = 0;
  AgentSnapshot claude;
  AgentSnapshot codex;
};

struct TelemetrySnapshot {
  bool sensorOk = false;
  float temperatureC = NAN;
  float humidityPercent = NAN;
  uint16_t batteryMillivolts = 0;
  // Voltage-derived estimate only; -1 means unavailable. The board has no
  // dedicated fuel-gauge IC, so the UI deliberately renders this with '~'.
  int8_t batteryPercent = -1;
  uint32_t sampledAtMs = 0;
};

struct NetworkSnapshot {
  bool connected = false;
  char ssid[kSsidTextSize] = {};
  char ip[kIpTextSize] = "0.0.0.0";
  int16_t rssi = -127;
};

struct LatencyTargetSnapshot {
  char name[kLatencyNameSize] = {};
  int32_t latencyMs = -1;
  uint32_t sampledAtMs = 0;
  bool reachable = false;
};

struct NetworkQualitySnapshot {
  LatencyTargetSnapshot targets[kLatencyTargetCount];
  uint32_t updatedAtMs = 0;
  uint32_t completedRounds = 0;
};

struct UiModel {
  NetworkSnapshot network;
  NetworkQualitySnapshot networkQuality;
  BridgeEndpoint endpoint;
  BridgeSnapshot bridge;
  TelemetrySnapshot telemetry;
  DisplayMode mode = DisplayMode::Auto;
  uint8_t page = 0;
  uint32_t revision = 1;
  uint32_t lastDisplayAtMs = 0;
};

inline bool agentSemanticallyEqual(const AgentSnapshot& lhs,
                                   const AgentSnapshot& rhs) {
  return strcmp(lhs.status, rhs.status) == 0 &&
         lhs.tokensToday == rhs.tokensToday &&
         lhs.sessionMinutes == rhs.sessionMinutes &&
         lhs.sessionWindowMinutes == rhs.sessionWindowMinutes &&
         lhs.primaryPercent == rhs.primaryPercent &&
         lhs.primaryWindowMinutes == rhs.primaryWindowMinutes &&
         lhs.primaryResetTimestamp == rhs.primaryResetTimestamp &&
         lhs.secondaryPercent == rhs.secondaryPercent &&
         lhs.secondaryWindowMinutes == rhs.secondaryWindowMinutes &&
         lhs.secondaryResetTimestamp == rhs.secondaryResetTimestamp &&
         lhs.needsInput == rhs.needsInput;
}

inline bool bridgeSemanticallyEqual(const BridgeSnapshot& lhs,
                                    const BridgeSnapshot& rhs) {
  return lhs.reachable == rhs.reachable &&
         agentSemanticallyEqual(lhs.codex, rhs.codex);
}

inline bool bridgeIsUrgent(const BridgeSnapshot& snapshot) {
  return snapshot.codex.needsInput || !snapshot.reachable;
}

}  // namespace app

#endif  // EPAPER75_APP_MODELS_H_
