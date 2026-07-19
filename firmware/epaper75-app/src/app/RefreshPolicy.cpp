#include "app/RefreshPolicy.h"

#include <math.h>
#include <string.h>

#include "board/BoardConfig.h"

namespace app {
namespace {

constexpr RefreshWindow kFullScreen = {0, 0, board::kEpdWidth,
                                        board::kEpdHeight};

// All x coordinates and widths are byte-aligned for GD7965 RAM addressing.
// A changed set is merged into one rectangle so one UI cycle never incurs
// several consecutive 18-second partial waveforms.
constexpr RefreshWindow kHeaderDynamic = {672, 8, 112, 54};
constexpr RefreshWindow kHomeAccount = {16, 72, 312, 376};
constexpr RefreshWindow kHomeFleetSummary = {352, 72, 432, 48};
constexpr RefreshWindow kHomeDeviceRows[kAgentDeviceCount] = {
    {352, 116, 432, 105},
    {352, 217, 432, 105},
    {352, 318, 432, 130},
};
constexpr RefreshWindow kSystemLeft = {16, 72, 368, 328};
constexpr RefreshWindow kSystemRight = {400, 72, 384, 328};

bool enteredReachability(const AgentDeviceSnapshot& previous,
                         const AgentDeviceSnapshot& current,
                         AgentReachability target) {
  return current.reachability == target && previous.reachability != target;
}

bool becameUrgent(const AgentFleetSnapshot& previous,
                  const AgentFleetSnapshot& current) {
  for (size_t i = 0; i < kAgentDeviceCount; ++i) {
    const AgentDeviceSnapshot& before = previous.devices[i];
    const AgentDeviceSnapshot& after = current.devices[i];

    const bool newlyWaitingApproval =
        (after.workload == WorkloadState::WaitingApproval &&
         before.workload != WorkloadState::WaitingApproval) ||
        (after.waitingApproval > 0 &&
         after.waitingApproval > before.waitingApproval);
    const bool newlyWaitingInput =
        (after.workload == WorkloadState::WaitingInput &&
         before.workload != WorkloadState::WaitingInput) ||
        (after.waitingInput > 0 &&
         after.waitingInput > before.waitingInput);
    const bool lostOnline = deviceOnline(before) && !deviceOnline(after);
    const bool newWorkloadError =
        (after.workload == WorkloadState::Error &&
         before.workload != WorkloadState::Error) ||
        (after.errors > 0 && after.errors > before.errors);
    const bool newConnectionFailure =
        enteredReachability(before, after, AgentReachability::Offline) ||
        enteredReachability(before, after, AgentReachability::AuthError) ||
        enteredReachability(before, after,
                            AgentReachability::ProtocolError);

    if (newlyWaitingApproval || newlyWaitingInput || lostOnline ||
        newWorkloadError || newConnectionFailure) {
      return true;
    }
  }
  return false;
}

void includeWindow(RefreshWindow& destination, bool& haveDestination,
                   const RefreshWindow& addition) {
  if (!addition.valid()) return;
  if (!haveDestination) {
    destination = addition;
    haveDestination = true;
    return;
  }

  const uint16_t left = min(destination.x, addition.x);
  const uint16_t top = min(destination.y, addition.y);
  const uint16_t right = max(
      static_cast<uint16_t>(destination.x + destination.width),
      static_cast<uint16_t>(addition.x + addition.width));
  const uint16_t bottom = max(
      static_cast<uint16_t>(destination.y + destination.height),
      static_cast<uint16_t>(addition.y + addition.height));
  destination = {left, top, static_cast<uint16_t>(right - left),
                 static_cast<uint16_t>(bottom - top)};
}

uint8_t verifiedCount(const AgentFleetSnapshot& fleet) {
  uint8_t result = 0;
  for (size_t i = 0; i < kAgentDeviceCount; ++i) {
    if (fleet.devices[i].identityVerified) ++result;
  }
  return result;
}

uint8_t exactStateCount(const AgentFleetSnapshot& fleet) {
  uint8_t result = 0;
  for (size_t i = 0; i < kAgentDeviceCount; ++i) {
    if (fleet.devices[i].stateExact) ++result;
  }
  return result;
}

bool accountDisplayEqual(const AccountQuotaSnapshot& left,
                         const AccountQuotaSnapshot& right) {
  // Match RuntimeState's semantic filter: a timestamp-only poll must not
  // enlarge an otherwise local device-row refresh into both columns.
  return accountQuotaSemanticallyEqual(left, right);
}

bool telemetryDisplayEqual(const TelemetrySnapshot& left,
                           const TelemetrySnapshot& right) {
  if (left.sensorOk != right.sensorOk ||
      left.batteryPercent != right.batteryPercent) {
    return false;
  }
  if (left.sensorOk) {
    if (isnan(left.temperatureC) != isnan(right.temperatureC) ||
        isnan(left.humidityPercent) != isnan(right.humidityPercent)) {
      return false;
    }
    if (!isnan(left.temperatureC) &&
        lroundf(left.temperatureC * 10.0f) !=
            lroundf(right.temperatureC * 10.0f)) {
      return false;
    }
    if (!isnan(left.humidityPercent) &&
        lroundf(left.humidityPercent) != lroundf(right.humidityPercent)) {
      return false;
    }
  }
  // The System page prints volts with two decimal places.
  return (left.batteryMillivolts + 5U) / 10U ==
         (right.batteryMillivolts + 5U) / 10U;
}

bool systemLeftDisplayEqual(const UiModel& left, const UiModel& right) {
  return left.network.connected == right.network.connected &&
         // RSSI jitter is deliberately non-semantic in RuntimeState. It can
         // ride along with another visible System update but cannot cause one.
         fleetOnlineCount(left.fleet) == fleetOnlineCount(right.fleet) &&
         fleetActionCount(left.fleet) == fleetActionCount(right.fleet) &&
         left.fleet.account.availability == right.fleet.account.availability &&
         left.fleet.account.stale == right.fleet.account.stale &&
         left.fleet.account.sourceIndex == right.fleet.account.sourceIndex &&
         verifiedCount(left.fleet) == verifiedCount(right.fleet) &&
         exactStateCount(left.fleet) == exactStateCount(right.fleet);
}

RefreshPlan dirtyPlan(const UiModel& displayed, const UiModel& desired) {
  if ((displayed.page % 2U) != (desired.page % 2U)) {
    return {RefreshKind::Full, kFullScreen};
  }

  RefreshWindow dirty;
  bool haveDirty = false;
  const bool headerChanged =
      displayed.network.connected != desired.network.connected ||
      fleetOnlineCount(displayed.fleet) != fleetOnlineCount(desired.fleet);
  if (headerChanged) includeWindow(dirty, haveDirty, kHeaderDynamic);

  if ((desired.page % 2U) == 0U) {
    if (!accountDisplayEqual(displayed.fleet.account,
                             desired.fleet.account)) {
      includeWindow(dirty, haveDirty, kHomeAccount);
    }
    if (fleetOnlineCount(displayed.fleet) !=
            fleetOnlineCount(desired.fleet) ||
        fleetActionCount(displayed.fleet) !=
            fleetActionCount(desired.fleet)) {
      includeWindow(dirty, haveDirty, kHomeFleetSummary);
    }
    for (size_t i = 0; i < kAgentDeviceCount; ++i) {
      if (!agentDeviceSemanticallyEqual(displayed.fleet.devices[i],
                                        desired.fleet.devices[i])) {
        includeWindow(dirty, haveDirty, kHomeDeviceRows[i]);
      }
    }
  } else {
    if (!systemLeftDisplayEqual(displayed, desired)) {
      includeWindow(dirty, haveDirty, kSystemLeft);
    }
    if (!telemetryDisplayEqual(displayed.telemetry, desired.telemetry)) {
      includeWindow(dirty, haveDirty, kSystemRight);
    }
  }

  if (!haveDirty) return {};
  const uint32_t dirtyArea =
      static_cast<uint32_t>(dirty.width) * dirty.height;
  const uint32_t screenArea =
      static_cast<uint32_t>(board::kEpdWidth) * board::kEpdHeight;
  if (dirtyArea * 100U >=
      screenArea * board::kPartialWindowFullThresholdPercent) {
    return {RefreshKind::Full, kFullScreen};
  }
  return {RefreshKind::Partial, dirty};
}

}  // namespace

const char* refreshKindName(RefreshKind kind) {
  switch (kind) {
    case RefreshKind::Partial:
      return "partial";
    case RefreshKind::Full:
      return "full";
    case RefreshKind::None:
    default:
      return "none";
  }
}

void RefreshPolicy::observe(const UiModel& model, uint32_t nowMs) {
  if (haveObservedModel_ && model.revision == observedRevision_) return;

  const bool urgentTransition =
      havePreviouslyObservedFleet_ &&
      becameUrgent(previouslyObservedFleet_, model.fleet);

  observedModel_ = model;
  observedRevision_ = model.revision;
  haveObservedModel_ = true;
  previouslyObservedFleet_ = model.fleet;
  havePreviouslyObservedFleet_ = true;
  changedAtMs_ = nowMs;
  urgentPending_ = urgentPending_ || urgentTransition;
  recalculatePlan(nowMs);
}

void RefreshPolicy::forceFull(uint32_t nowMs) {
  if (!pending_) changedAtMs_ = nowMs;
  forcedFullPending_ = true;
  pendingPlan_ = {RefreshKind::Full, kFullScreen};
  pending_ = true;
}

bool RefreshPolicy::due(uint32_t nowMs) const {
  if (!pending_ || pendingPlan_.kind == RefreshKind::None) return false;
  if (nowMs - changedAtMs_ < board::kUiCoalesceMs) return false;
  if (lastRefreshAtMs_ == 0) return true;

  const uint32_t floorMs = (urgentPending_ || forcedFullPending_)
                               ? board::kUrgentRefreshFloorMs
                               : board::kOrdinaryRefreshFloorMs;
  return nowMs - lastRefreshAtMs_ >= floorMs;
}

void RefreshPolicy::markCompleted(const UiModel& displayedModel,
                                  RefreshKind completedKind, uint32_t nowMs) {
  displayedModel_ = displayedModel;
  haveDisplayedModel_ = true;
  lastRefreshAtMs_ = nowMs;
  if (completedKind == RefreshKind::Full) {
    partialRefreshesSinceFull_ = 0;
    lastFullRefreshAtMs_ = nowMs;
  } else if (completedKind == RefreshKind::Partial &&
             partialRefreshesSinceFull_ < UINT16_MAX) {
    ++partialRefreshesSinceFull_;
  }

  forcedFullPending_ = false;
  urgentPending_ = false;
  recalculatePlan(nowMs);
  if (pending_) changedAtMs_ = nowMs;
}

void RefreshPolicy::markAbandoned(uint32_t nowMs) {
  lastRefreshAtMs_ = nowMs;
  pendingPlan_ = {};
  pending_ = false;
  urgentPending_ = false;
  forcedFullPending_ = false;
}

void RefreshPolicy::recalculatePlan(uint32_t nowMs) {
  if (forcedFullPending_ || !haveDisplayedModel_) {
    pendingPlan_ = {RefreshKind::Full, kFullScreen};
    pending_ = haveObservedModel_ || forcedFullPending_;
    return;
  }
  if (!haveObservedModel_) {
    pendingPlan_ = {};
    pending_ = false;
    return;
  }

  pendingPlan_ = dirtyPlan(displayedModel_, observedModel_);
  if (pendingPlan_.kind == RefreshKind::Partial) {
    const bool partialLimitReached =
        partialRefreshesSinceFull_ >= board::kMaxPartialRefreshesBeforeFull;
    const bool maintenanceDue =
        lastFullRefreshAtMs_ != 0 &&
        nowMs - lastFullRefreshAtMs_ >= board::kFullRefreshMaintenanceMs;
    if (partialLimitReached || maintenanceDue) {
      pendingPlan_ = {RefreshKind::Full, kFullScreen};
    }
  }
  pending_ = pendingPlan_.kind != RefreshKind::None;
  if (!pending_ && !forcedFullPending_) urgentPending_ = false;
}

}  // namespace app
