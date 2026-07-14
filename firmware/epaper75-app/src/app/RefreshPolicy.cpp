#include "app/RefreshPolicy.h"

#include "board/BoardConfig.h"

namespace app {

void RefreshPolicy::observe(const UiModel& model, uint32_t nowMs) {
  if (model.revision == observedRevision_) return;

  const bool firstObservation = observedRevision_ == 0;
  const bool newlyNeedsInput =
      (model.bridge.codex.needsInput && !observedCodexNeedsInput_);
  const bool bridgeDropped = observedBridgeReachable_ &&
                             !model.bridge.reachable;

  observedRevision_ = model.revision;
  observedBridgeReachable_ = model.bridge.reachable;
  observedCodexNeedsInput_ = model.bridge.codex.needsInput;
  changedAtMs_ = nowMs;
  pending_ = true;
  urgentPending_ = urgentPending_ || newlyNeedsInput || bridgeDropped;

  // The first complete model should appear without a refresh-floor delay.
  if (firstObservation) lastRefreshAtMs_ = 0;
}

void RefreshPolicy::force(uint32_t nowMs) {
  if (!pending_) changedAtMs_ = nowMs;
  pending_ = true;
  forcedPending_ = true;
}

bool RefreshPolicy::due(uint32_t nowMs) const {
  if (!pending_) return false;
  if (nowMs - changedAtMs_ < board::kUiCoalesceMs) return false;
  if (lastRefreshAtMs_ == 0) return true;

  const uint32_t floorMs = (urgentPending_ || forcedPending_)
                               ? board::kUrgentRefreshFloorMs
                               : board::kOrdinaryRefreshFloorMs;
  return nowMs - lastRefreshAtMs_ >= floorMs;
}

void RefreshPolicy::markCompleted(uint32_t displayedRevision, uint32_t nowMs) {
  displayedRevision_ = displayedRevision;
  lastRefreshAtMs_ = nowMs;
  pending_ = displayedRevision_ != observedRevision_;
  urgentPending_ = false;
  forcedPending_ = false;
  if (pending_) changedAtMs_ = nowMs;
}

}  // namespace app
