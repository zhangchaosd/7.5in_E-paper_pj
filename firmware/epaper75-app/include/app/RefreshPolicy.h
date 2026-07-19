#pragma once

#include <Arduino.h>

#include "Models.h"

namespace app {

// Filters changes that are not visible on the current page, coalesces routine
// changes, and schedules the panel's proven full-screen waveform. Physical
// testing found the Z08 partial-window result unacceptable on this recovered
// panel, so this policy intentionally has no partial-refresh plan.
class RefreshPolicy {
 public:
  void observe(const UiModel& model, uint32_t nowMs);
  void force(uint32_t nowMs);
  bool due(uint32_t nowMs) const;
  void markCompleted(const UiModel& displayedModel, uint32_t nowMs);
  void markAbandoned(uint32_t nowMs);

  bool pending() const { return pending_; }
  uint32_t lastRefreshAtMs() const { return lastRefreshAtMs_; }

 private:
  void recalculatePending();

  UiModel observedModel_;
  UiModel displayedModel_;
  AgentFleetSnapshot previouslyObservedFleet_;
  uint32_t observedRevision_ = 0;
  uint32_t changedAtMs_ = 0;
  uint32_t lastRefreshAtMs_ = 0;
  bool pending_ = false;
  bool urgentPending_ = false;
  bool forcedPending_ = false;
  bool haveObservedModel_ = false;
  bool havePreviouslyObservedFleet_ = false;
  bool haveDisplayedModel_ = false;
};

}  // namespace app
