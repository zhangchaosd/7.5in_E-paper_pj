#pragma once
#ifndef EPAPER75_APP_RUNTIME_STATE_H_
#define EPAPER75_APP_RUNTIME_STATE_H_

#include <Arduino.h>

#include "Models.h"

namespace app {

// Thread-safe POD state shared by Arduino loop (core 1) and the HTTP service
// task (core 0).  No String or heap-owned data is stored inside the lock.
class RuntimeState {
 public:
  RuntimeState();

  UiModel snapshot() const;

  bool updateNetwork(const NetworkSnapshot& value);
  bool updateNetworkQuality(const NetworkQualitySnapshot& value);
  bool updateTelemetry(const TelemetrySnapshot& value);
  bool updateFleet(const AgentFleetSnapshot& value);
  bool setDisplayMode(DisplayMode value);
  bool setPage(uint8_t value);

  void requestRefresh();
  bool consumeRefreshRequest();
  void markDisplayed(uint32_t atMs);

 private:
  void bumpRevisionLocked();

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  UiModel model_;
  bool forceRefresh_ = true;
};

}  // namespace app

#endif  // EPAPER75_APP_RUNTIME_STATE_H_
