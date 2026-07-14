#pragma once

#include <Arduino.h>

#include "app/Models.h"
#include "ui/Renderer.h"

namespace ui {

// Stored in RTC slow memory with a checksum. It can survive deep sleep and
// resets that leave the RTC domain intact. EN/CHIP_PU, brownout, or full power
// loss may clear it; those cases deliberately fall back to Unknown handling.
enum class PanelJournalState : uint32_t {
  Unknown = 0,
  Initializing = 1,
  Refreshing = 2,
  PoweredOff = 3,
  Hibernated = 4,
  Recovering = 5,
  Fault = 6,
};

enum class EpdError : uint8_t {
  None = 0,
  ReentrantRefresh,
  PersistedFault,
  SpiBeginFailed,
  BusyBeforeInit,
  BusyAfterInit,
  RefreshTimedOut,
  DriverOperationFailed,
  BusyAfterRefresh,
};

const char* panelJournalStateName(PanelJournalState state);
const char* epdErrorName(EpdError error);

class EpdController {
 public:
  EpdController();

  EpdController(const EpdController&) = delete;
  EpdController& operator=(const EpdController&) = delete;

  // The only public operation capable of changing the physical display.
  // It performs one full refresh and powers panel voltages off afterward.
  // On failure it permits exactly one reset/recovery attempt, then latches a
  // fault in RTC memory instead of retrying indefinitely.
  bool refresh(const app::UiModel& model);

  bool isFaultLocked() const { return faultLocked_; }
  PanelJournalState journalState() const { return journalState_; }
  EpdError lastError() const { return lastError_; }
  uint32_t lastRefreshDurationMs() const { return lastRefreshDurationMs_; }

 private:
  bool prepareInterruptedRefresh(PanelJournalState previousState);
  bool performRefresh(const app::UiModel& model, bool recoveryAttempt);
  bool initializePanel(bool recoveryAttempt);
  bool waitUntilReady(uint32_t timeoutMs) const;
  void shutdownAfterFailure();
  void hardResetPanel();
  void releaseBusHigh();
  void setJournal(PanelJournalState state);
  void lockFault(EpdError error);

  Renderer renderer_;
  PanelJournalState journalState_ = PanelJournalState::Unknown;
  EpdError lastError_ = EpdError::None;
  uint32_t lastRefreshDurationMs_ = 0;
  bool faultLocked_ = false;
  bool refreshInProgress_ = false;
  bool busActive_ = false;
};

}  // namespace ui
