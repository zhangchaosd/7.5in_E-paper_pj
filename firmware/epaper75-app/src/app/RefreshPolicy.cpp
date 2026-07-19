#include "app/RefreshPolicy.h"

#include <math.h>

#include "board/BoardConfig.h"

namespace app {
namespace {

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
  return (left.batteryMillivolts + 5U) / 10U ==
         (right.batteryMillivolts + 5U) / 10U;
}

bool systemDisplayEqual(const UiModel& left, const UiModel& right) {
  return left.network.connected == right.network.connected &&
         fleetOnlineCount(left.fleet) == fleetOnlineCount(right.fleet) &&
         fleetActionCount(left.fleet) == fleetActionCount(right.fleet) &&
         left.fleet.account.availability == right.fleet.account.availability &&
         left.fleet.account.stale == right.fleet.account.stale &&
         left.fleet.account.sourceIndex == right.fleet.account.sourceIndex &&
         verifiedCount(left.fleet) == verifiedCount(right.fleet) &&
         exactStateCount(left.fleet) == exactStateCount(right.fleet) &&
         telemetryDisplayEqual(left.telemetry, right.telemetry);
}

bool dashboardDisplayEqual(const UiModel& left, const UiModel& right) {
  if (left.network.connected != right.network.connected ||
      !accountQuotaSemanticallyEqual(left.fleet.account,
                                     right.fleet.account)) {
    return false;
  }
  for (size_t i = 0; i < kAgentDeviceCount; ++i) {
    if (!agentDeviceSemanticallyEqual(left.fleet.devices[i],
                                      right.fleet.devices[i])) {
      return false;
    }
  }
  return true;
}

bool visibleDisplayEqual(const UiModel& left, const UiModel& right) {
  if ((left.page % 2U) != (right.page % 2U)) return false;
  return (right.page % 2U) == 0U ? dashboardDisplayEqual(left, right)
                                 : systemDisplayEqual(left, right);
}

}  // namespace

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
  recalculatePending();
}

void RefreshPolicy::force(uint32_t nowMs) {
  if (!pending_) changedAtMs_ = nowMs;
  forcedPending_ = true;
  pending_ = true;
}

bool RefreshPolicy::due(uint32_t nowMs) const {
  if (!pending_) return false;
  if (nowMs - changedAtMs_ < board::kUiCoalesceMs) return false;
  if (lastRefreshAtMs_ == 0) return true;

  const uint32_t floorMs =
      forcedPending_ ? board::kForcedRefreshFloorMs
                     : (urgentPending_ ? board::kUrgentRefreshFloorMs
                                       : board::kRoutineRefreshFloorMs);
  return nowMs - lastRefreshAtMs_ >= floorMs;
}

void RefreshPolicy::markCompleted(const UiModel& displayedModel,
                                  uint32_t nowMs) {
  displayedModel_ = displayedModel;
  haveDisplayedModel_ = true;
  lastRefreshAtMs_ = nowMs;
  forcedPending_ = false;
  urgentPending_ = false;
  recalculatePending();
  if (pending_) changedAtMs_ = nowMs;
}

void RefreshPolicy::markAbandoned(uint32_t nowMs) {
  lastRefreshAtMs_ = nowMs;
  pending_ = false;
  urgentPending_ = false;
  forcedPending_ = false;
}

void RefreshPolicy::recalculatePending() {
  pending_ = forcedPending_ ||
             (haveObservedModel_ &&
              (!haveDisplayedModel_ ||
               !visibleDisplayEqual(displayedModel_, observedModel_)));
  if (!pending_) urgentPending_ = false;
}

}  // namespace app
