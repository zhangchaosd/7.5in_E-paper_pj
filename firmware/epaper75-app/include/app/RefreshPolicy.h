#pragma once

#include <Arduino.h>

#include "Models.h"

namespace app {

enum class RefreshKind : uint8_t {
  None = 0,
  Partial,
  Full,
};

struct RefreshWindow {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;

  bool valid() const { return width > 0 && height > 0; }
};

struct RefreshPlan {
  RefreshKind kind = RefreshKind::None;
  RefreshWindow window;
};

const char* refreshKindName(RefreshKind kind);

// Coalesces model changes, calculates one bounded dirty window, and promotes
// partial work to an occasional full cleanup. The panel has native partial
// window support but no fast-partial waveform, so a due cycle always issues at
// most one display refresh command.
class RefreshPolicy {
 public:
  void observe(const UiModel& model, uint32_t nowMs);
  void forceFull(uint32_t nowMs);
  bool due(uint32_t nowMs) const;
  RefreshPlan plan() const { return pendingPlan_; }
  void markCompleted(const UiModel& displayedModel,
                     RefreshKind completedKind, uint32_t nowMs);
  void markAbandoned(uint32_t nowMs);

  bool pending() const { return pending_; }
  uint32_t lastRefreshAtMs() const { return lastRefreshAtMs_; }
  uint32_t lastFullRefreshAtMs() const { return lastFullRefreshAtMs_; }
  uint16_t partialRefreshesSinceFull() const {
    return partialRefreshesSinceFull_;
  }

 private:
  void recalculatePlan(uint32_t nowMs);

  UiModel observedModel_;
  UiModel displayedModel_;
  AgentFleetSnapshot previouslyObservedFleet_;
  RefreshPlan pendingPlan_;
  uint32_t observedRevision_ = 0;
  uint32_t changedAtMs_ = 0;
  uint32_t lastRefreshAtMs_ = 0;
  uint32_t lastFullRefreshAtMs_ = 0;
  uint16_t partialRefreshesSinceFull_ = 0;
  bool pending_ = false;
  bool urgentPending_ = false;
  bool forcedFullPending_ = false;
  bool haveObservedModel_ = false;
  bool havePreviouslyObservedFleet_ = false;
  bool haveDisplayedModel_ = false;
};

}  // namespace app
