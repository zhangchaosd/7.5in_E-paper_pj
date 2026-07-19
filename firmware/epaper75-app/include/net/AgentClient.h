#pragma once
#ifndef EPAPER75_APP_AGENT_CLIENT_H_
#define EPAPER75_APP_AGENT_CLIENT_H_

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "SettingsStore.h"
#include "app/RuntimeState.h"

namespace net {

enum class AgentPollResult : uint8_t {
  NotStarted = 0,
  Ok,
  WifiDisconnected,
  SettingsMissing,
  SettingsPersistFailed,
  HttpBeginFailed,
  HttpError,
  Unauthorized,
  ResponseTooLarge,
  InvalidJson,
  IncompatibleSchema,
  IdentityMismatch,
  Superseded,
};

const char* agentPollResultName(AgentPollResult result);

// Polls the three configured Codex Monitor Agent installations sequentially
// from a low-priority Core 0 task. Credentials and endpoint identities remain
// private to this class and SettingsStore; only bounded display data is copied
// into RuntimeState.
class AgentClient {
 public:
  AgentClient(app::RuntimeState& state, SettingsStore& settings,
              uint32_t interDevicePollMs = 5000);
  ~AgentClient();

  AgentClient(const AgentClient&) = delete;
  AgentClient& operator=(const AgentClient&) = delete;

  bool begin();
  void end();
  void requestImmediatePoll();

  bool running() const;
  AgentPollResult lastResult() const;
  int lastHttpCode() const;
  uint32_t lastDurationMs() const;

 private:
  struct SlotRuntime {
    app::AgentDeviceSnapshot snapshot;
    char installationId[kAgentInstallationIdSize] = {};
    int64_t lastGeneratedTimestamp = 0;
    uint32_t nextAttemptAtMs = 0;
    uint8_t consecutiveFailures = 0;
    bool identityVerified = false;
    bool authLocked = false;
    bool protocolLocked = false;
  };

  static void taskEntry(void* context);
  void run();
  bool reloadSettings(bool force);
  bool pollSlot(size_t slotIndex);
  void publishFleet();
  void publishWifiUnavailable();
  void publishFailure(size_t slotIndex, AgentPollResult result,
                      int httpCode, uint32_t startedAtMs);
  void resetForSettings(const AgentFleetSettings& settings);
  void maybePollQuota(size_t slotIndex, uint32_t nowMs);
  void finishQuotaCycleUnavailable(uint32_t nowMs);
  void recordResult(AgentPollResult result, int httpCode,
                    uint32_t durationMs);

  app::RuntimeState& state_;
  SettingsStore& settings_;
  const uint32_t interDevicePollMs_;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t taskHandle_ = nullptr;
  volatile bool stopRequested_ = false;
  volatile bool running_ = false;
  bool immediatePoll_ = true;
  bool hasSettings_ = false;
  bool wifiOfflinePublished_ = false;
  AgentFleetSettings activeSettings_;
  SlotRuntime slots_[app::kAgentDeviceCount];
  app::AccountQuotaSnapshot account_;
  size_t nextSlotIndex_ = 0;
  uint32_t nextPollAtMs_ = 0;
  uint32_t lastSettingsCheckAtMs_ = 0;
  uint32_t nextQuotaAtMs_ = 0;
  uint32_t quotaCycleStartedAtMs_ = 0;
  uint8_t quotaAttemptMask_ = 0;
  bool quotaCycleActive_ = false;
  AgentPollResult lastResult_ = AgentPollResult::NotStarted;
  int lastHttpCode_ = 0;
  uint32_t lastDurationMs_ = 0;
};

}  // namespace net

#endif  // EPAPER75_APP_AGENT_CLIENT_H_
