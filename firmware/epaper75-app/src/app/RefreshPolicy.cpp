#include "app/RefreshPolicy.h"

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
        enteredReachability(before, after, AgentReachability::ProtocolError);

    if (newlyWaitingApproval || newlyWaitingInput || lostOnline ||
        newWorkloadError || newConnectionFailure) {
      return true;
    }
  }
  return false;
}

}  // namespace

void RefreshPolicy::observe(const UiModel& model, uint32_t nowMs) {
  if (model.revision == observedRevision_) return;

  const bool firstObservation = observedRevision_ == 0;
  const bool urgentTransition =
      haveObservedFleet_ && becameUrgent(observedFleet_, model.fleet);

  observedRevision_ = model.revision;
  observedFleet_ = model.fleet;
  haveObservedFleet_ = true;
  changedAtMs_ = nowMs;
  pending_ = true;
  urgentPending_ = urgentPending_ || urgentTransition;

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
