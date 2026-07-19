#pragma once
#ifndef EPAPER75_APP_MODELS_H_
#define EPAPER75_APP_MODELS_H_

#include <Arduino.h>
#include <math.h>
#include <string.h>

namespace app {

// Configuration storage uses this bound, but hosts never enter UiModel.
constexpr size_t kHostTextSize = 64;
constexpr size_t kSsidTextSize = 33;
constexpr size_t kIpTextSize = 16;
constexpr size_t kLatencyTargetCount = 4;
constexpr size_t kLatencyNameSize = 12;
constexpr size_t kAgentDeviceCount = 3;
constexpr size_t kDeviceLabelSize = 16;
constexpr size_t kTaskTextSize = 34;
constexpr uint8_t kNoAgentDevice = UINT8_MAX;
constexpr uint32_t kUnknownActivityAgeSeconds = UINT32_MAX;

enum class DataAvailability : uint8_t {
  Unknown = 0,
  Available,
  Unavailable,
};

enum class AgentReachability : uint8_t {
  Unconfigured = 0,
  Connecting,
  Online,
  Recovering,
  Offline,
  AuthError,
  ProtocolError,
};

enum class WorkloadState : uint8_t {
  Running = 0,
  WaitingApproval,
  WaitingInput,
  Idle,
  Error,
  Unknown,
};

// Kept for the existing display settings/API surface. Fleet data itself is
// Codex-only; Auto and Codex therefore render the same account data.
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
  if (strcasecmp(value, "auto") == 0) {
    mode = DisplayMode::Auto;
  } else if (strcasecmp(value, "claude") == 0) {
    mode = DisplayMode::Claude;
  } else if (strcasecmp(value, "codex") == 0) {
    mode = DisplayMode::Codex;
  } else {
    return false;
  }
  return true;
}

inline const char* dataAvailabilityName(DataAvailability value) {
  switch (value) {
    case DataAvailability::Available:
      return "available";
    case DataAvailability::Unavailable:
      return "unavailable";
    case DataAvailability::Unknown:
    default:
      return "unknown";
  }
}

inline const char* dataAvailabilityLabel(DataAvailability value) {
  switch (value) {
    case DataAvailability::Available:
      return "CURRENT";
    case DataAvailability::Unavailable:
      return "UNAVAILABLE";
    case DataAvailability::Unknown:
    default:
      return "UNKNOWN";
  }
}

inline const char* agentReachabilityName(AgentReachability value) {
  switch (value) {
    case AgentReachability::Unconfigured:
      return "unconfigured";
    case AgentReachability::Connecting:
      return "connecting";
    case AgentReachability::Online:
      return "online";
    case AgentReachability::Recovering:
      return "recovering";
    case AgentReachability::Offline:
      return "offline";
    case AgentReachability::AuthError:
      return "auth_error";
    case AgentReachability::ProtocolError:
      return "protocol_error";
    default:
      return "unconfigured";
  }
}

inline const char* agentReachabilityLabel(AgentReachability value) {
  switch (value) {
    case AgentReachability::Unconfigured:
      return "NOT SET";
    case AgentReachability::Connecting:
      return "CONNECTING";
    case AgentReachability::Online:
      return "ONLINE";
    case AgentReachability::Recovering:
      return "RECOVERING";
    case AgentReachability::Offline:
      return "OFFLINE";
    case AgentReachability::AuthError:
      return "AUTH ERROR";
    case AgentReachability::ProtocolError:
      return "PROTOCOL ERROR";
    default:
      return "NOT SET";
  }
}

inline const char* workloadStateName(WorkloadState value) {
  switch (value) {
    case WorkloadState::Running:
      return "RUNNING";
    case WorkloadState::WaitingApproval:
      return "WAITING_APPROVAL";
    case WorkloadState::WaitingInput:
      return "WAITING_INPUT";
    case WorkloadState::Idle:
      return "IDLE";
    case WorkloadState::Error:
      return "ERROR";
    case WorkloadState::Unknown:
    default:
      return "UNKNOWN";
  }
}

inline const char* workloadStateLabel(WorkloadState value) {
  switch (value) {
    case WorkloadState::Running:
      return "RUNNING";
    case WorkloadState::WaitingApproval:
      return "APPROVAL";
    case WorkloadState::WaitingInput:
      return "INPUT";
    case WorkloadState::Idle:
      return "IDLE";
    case WorkloadState::Error:
      return "ERROR";
    case WorkloadState::Unknown:
    default:
      return "UNKNOWN";
  }
}

template <size_t N>
inline void copyText(char (&destination)[N], const char* source) {
  if (source == nullptr) source = "";
  strlcpy(destination, source, N);
}

struct AccountQuotaSnapshot {
  DataAvailability availability = DataAvailability::Unknown;
  // The upstream API reports consumption. The renderer derives remaining as
  // 100 - weeklyUsedPercent; -1 means no trustworthy value is available.
  int16_t weeklyUsedPercent = -1;
  int64_t weeklyResetTimestamp = 0;
  int64_t sourceGeneratedTimestamp = 0;
  uint32_t receivedAtMs = 0;
  uint8_t sourceIndex = kNoAgentDevice;
  bool stale = false;
};

struct AgentDeviceSnapshot {
  char label[kDeviceLabelSize] = {};
  char currentTask[kTaskTextSize] = {};
  AgentReachability reachability = AgentReachability::Unconfigured;
  WorkloadState workload = WorkloadState::Unknown;
  // Counts use -1 for fields omitted by a compatible future/older agent.
  int16_t activeWorkflows = -1;
  int16_t activeWorkers = -1;
  int16_t running = -1;
  int16_t waitingApproval = -1;
  int16_t waitingInput = -1;
  int16_t errors = -1;
  uint32_t activityAgeSecondsAtReceive = kUnknownActivityAgeSeconds;
  uint32_t receivedAtMs = 0;
  bool stale = false;
  bool identityVerified = false;
  bool stateExact = false;
};

struct AgentFleetSnapshot {
  AccountQuotaSnapshot account;
  AgentDeviceSnapshot devices[kAgentDeviceCount];
};

struct TelemetrySnapshot {
  bool sensorOk = false;
  float temperatureC = NAN;
  float humidityPercent = NAN;
  uint16_t batteryMillivolts = 0;
  // Voltage-derived estimate only; -1 means unavailable. The board has no
  // dedicated fuel-gauge IC, so any UI rendering must retain the '~' marker.
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
  AgentFleetSnapshot fleet;
  TelemetrySnapshot telemetry;
  DisplayMode mode = DisplayMode::Auto;
  uint8_t page = 0;
  uint32_t revision = 1;
  uint32_t lastDisplayAtMs = 0;
};

inline int16_t accountWeeklyRemainingPercent(
    const AccountQuotaSnapshot& account) {
  // Preserve the last valid value while the source is transiently stale. The
  // adjacent data-status label makes that uncertainty explicit.
  if (account.weeklyUsedPercent < 0) {
    return -1;
  }
  if (account.weeklyUsedPercent >= 100) return 0;
  return static_cast<int16_t>(100 - account.weeklyUsedPercent);
}

inline bool deviceOnline(const AgentDeviceSnapshot& device) {
  return device.reachability == AgentReachability::Online &&
         device.identityVerified && !device.stale;
}

inline bool deviceNeedsAction(const AgentDeviceSnapshot& device) {
  return device.workload == WorkloadState::WaitingApproval ||
         device.workload == WorkloadState::WaitingInput ||
         device.waitingApproval > 0 || device.waitingInput > 0;
}

inline bool deviceHasError(const AgentDeviceSnapshot& device) {
  return device.workload == WorkloadState::Error || device.errors > 0 ||
         device.reachability == AgentReachability::AuthError ||
         device.reachability == AgentReachability::ProtocolError;
}

inline uint8_t fleetOnlineCount(const AgentFleetSnapshot& fleet) {
  uint8_t count = 0;
  for (size_t i = 0; i < kAgentDeviceCount; ++i) {
    if (deviceOnline(fleet.devices[i])) ++count;
  }
  return count;
}

inline uint8_t fleetActionCount(const AgentFleetSnapshot& fleet) {
  uint8_t count = 0;
  for (size_t i = 0; i < kAgentDeviceCount; ++i) {
    if (deviceNeedsAction(fleet.devices[i])) ++count;
  }
  return count;
}

inline uint8_t fleetErrorCount(const AgentFleetSnapshot& fleet) {
  uint8_t count = 0;
  for (size_t i = 0; i < kAgentDeviceCount; ++i) {
    if (deviceHasError(fleet.devices[i])) ++count;
  }
  return count;
}

inline bool accountQuotaSemanticallyEqual(const AccountQuotaSnapshot& lhs,
                                          const AccountQuotaSnapshot& rhs) {
  return lhs.availability == rhs.availability &&
         lhs.weeklyUsedPercent == rhs.weeklyUsedPercent &&
         lhs.weeklyResetTimestamp == rhs.weeklyResetTimestamp &&
         lhs.sourceIndex == rhs.sourceIndex && lhs.stale == rhs.stale;
}

inline bool agentDeviceSemanticallyEqual(const AgentDeviceSnapshot& lhs,
                                         const AgentDeviceSnapshot& rhs) {
  return strcmp(lhs.label, rhs.label) == 0 &&
         strcmp(lhs.currentTask, rhs.currentTask) == 0 &&
         lhs.reachability == rhs.reachability &&
         lhs.workload == rhs.workload &&
         lhs.activeWorkflows == rhs.activeWorkflows &&
         lhs.activeWorkers == rhs.activeWorkers &&
         lhs.running == rhs.running &&
         lhs.waitingApproval == rhs.waitingApproval &&
         lhs.waitingInput == rhs.waitingInput && lhs.errors == rhs.errors &&
         lhs.stale == rhs.stale &&
         lhs.identityVerified == rhs.identityVerified &&
         lhs.stateExact == rhs.stateExact;
}

inline bool fleetSemanticallyEqual(const AgentFleetSnapshot& lhs,
                                   const AgentFleetSnapshot& rhs) {
  if (!accountQuotaSemanticallyEqual(lhs.account, rhs.account)) return false;
  for (size_t i = 0; i < kAgentDeviceCount; ++i) {
    if (!agentDeviceSemanticallyEqual(lhs.devices[i], rhs.devices[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace app

#endif  // EPAPER75_APP_MODELS_H_
