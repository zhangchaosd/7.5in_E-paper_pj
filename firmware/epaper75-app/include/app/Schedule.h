#pragma once

#include <stdint.h>

namespace app {

// What a timer wake should do.
enum class WakeKind : uint8_t {
  FullUpdate = 0,  // fetch everything, redraw if the visible page changed
  AlertCheck,      // fetch alerts only; full update only if they changed
};

struct WakePlan {
  int64_t epoch = 0;  // UTC seconds
  WakeKind kind = WakeKind::FullUpdate;
};

// Fixed Beijing-time schedule that keeps the panel and radio idle most of the
// day. Full updates (fetch + redraw when something visible changed):
//   00:02 (new calendar day), 06:30, 09:00, 12:00, 15:00, 18:00, 21:00.
// Alert checks (one small request, redraw only on a new/cancelled alert):
//   every remaining whole hour from 07:00 to 22:00.
// Nothing runs between 22:00 and 06:30 except the midnight date change.
WakePlan nextScheduledWake(int64_t nowEpoch);

// Retry after a failed fetch (no Wi-Fi, HTTP error...).
constexpr int64_t kRetryDelaySeconds = 15 * 60;
constexpr uint8_t kMaxRetriesPerSlot = 2;
// Clock unknown: try again this often.
constexpr int64_t kUnsyncedRetrySeconds = 30 * 60;
// Low battery: no radio; just re-check voltage this often.
constexpr int64_t kLowBatterySleepSeconds = 6 * 60 * 60;
// Woke this much earlier than planned (RTC drift): go back to sleep.
constexpr int64_t kEarlyWakeToleranceSeconds = 120;

}  // namespace app
