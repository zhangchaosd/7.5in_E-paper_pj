#pragma once

#include <Arduino.h>

#include "app/RuntimeState.h"

namespace net {

enum class BridgePollResult : uint8_t {
  NotStarted = 0,
  Ok,
  WifiDisconnected,
  EndpointMissing,
  HttpBeginFailed,
  HttpError,
  ResponseTooLarge,
  InvalidJson,
  Superseded,
};

const char* bridgePollResultName(BridgePollResult result);

class BridgeClient {
 public:
  explicit BridgeClient(app::RuntimeState& state,
                        uint32_t pollIntervalMs = 5000);

  void begin();
  void loop();
  bool pollNow();
  void requestImmediatePoll();

  BridgePollResult lastResult() const;
  int lastHttpCode() const;
  uint32_t lastDurationMs() const;

 private:
  bool endpointChanged(const app::BridgeEndpoint& endpoint) const;
  void rememberEndpoint(const app::BridgeEndpoint& endpoint);
  void publishUnavailable(BridgePollResult result, int httpCode,
                          uint32_t startedAtMs);
  void recordResult(BridgePollResult result, int httpCode,
                    uint32_t durationMs);

  app::RuntimeState& state_;
  const uint32_t pollIntervalMs_;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  app::BridgeEndpoint lastEndpoint_;
  bool hasLastEndpoint_ = false;
  bool begun_ = false;
  bool immediatePoll_ = true;
  uint8_t consecutiveFailures_ = 0;
  uint32_t nextPollAtMs_ = 0;
  BridgePollResult lastResult_ = BridgePollResult::NotStarted;
  int lastHttpCode_ = 0;
  uint32_t lastDurationMs_ = 0;
};

}  // namespace net
