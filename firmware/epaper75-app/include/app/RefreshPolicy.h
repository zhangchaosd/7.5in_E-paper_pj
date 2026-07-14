#pragma once

#include <Arduino.h>

#include "Models.h"

namespace app {

// Coalesces model changes and enforces conservative full-refresh floors.
// Polling may remain frequent; only semantic changes reach the panel.
class RefreshPolicy {
 public:
  void observe(const UiModel& model, uint32_t nowMs);
  void force(uint32_t nowMs);
  bool due(uint32_t nowMs) const;
  void markCompleted(uint32_t displayedRevision, uint32_t nowMs);

  bool pending() const { return pending_; }
  uint32_t lastRefreshAtMs() const { return lastRefreshAtMs_; }

 private:
  uint32_t observedRevision_ = 0;
  uint32_t displayedRevision_ = 0;
  uint32_t changedAtMs_ = 0;
  uint32_t lastRefreshAtMs_ = 0;
  bool pending_ = false;
  bool urgentPending_ = false;
  bool forcedPending_ = false;
  bool observedBridgeReachable_ = false;
  bool observedCodexNeedsInput_ = false;
};

}  // namespace app
