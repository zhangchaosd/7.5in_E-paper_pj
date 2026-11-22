#include "app/Schedule.h"

namespace app {
namespace {

constexpr int32_t kUtcOffsetSeconds = 8 * 60 * 60;

struct Slot {
  int16_t minute;  // minutes after local midnight
  WakeKind kind;
};

// Sorted by time of day.
constexpr Slot kSlots[] = {
    {0 * 60 + 2, WakeKind::FullUpdate},
    {6 * 60 + 30, WakeKind::FullUpdate},
    {7 * 60, WakeKind::AlertCheck},
    {8 * 60, WakeKind::AlertCheck},
    {9 * 60, WakeKind::FullUpdate},
    {10 * 60, WakeKind::AlertCheck},
    {11 * 60, WakeKind::AlertCheck},
    {12 * 60, WakeKind::FullUpdate},
    {13 * 60, WakeKind::AlertCheck},
    {14 * 60, WakeKind::AlertCheck},
    {15 * 60, WakeKind::FullUpdate},
    {16 * 60, WakeKind::AlertCheck},
    {17 * 60, WakeKind::AlertCheck},
    {18 * 60, WakeKind::FullUpdate},
    {19 * 60, WakeKind::AlertCheck},
    {20 * 60, WakeKind::AlertCheck},
    {21 * 60, WakeKind::FullUpdate},
    {22 * 60, WakeKind::AlertCheck},
};

}  // namespace

WakePlan nextScheduledWake(int64_t nowEpoch) {
  const int64_t local = nowEpoch + kUtcOffsetSeconds;
  int64_t day = local / 86400;
  int64_t secondOfDay = local % 86400;
  if (secondOfDay < 0) {
    secondOfDay += 86400;
    --day;
  }
  // Strictly after now (one second of slack avoids re-running a slot).
  for (const Slot& slot : kSlots) {
    const int64_t slotSecond = static_cast<int64_t>(slot.minute) * 60;
    if (slotSecond > secondOfDay) {
      WakePlan plan;
      plan.epoch = day * 86400 + slotSecond - kUtcOffsetSeconds;
      plan.kind = slot.kind;
      return plan;
    }
  }
  WakePlan plan;
  plan.epoch = (day + 1) * 86400 + static_cast<int64_t>(kSlots[0].minute) * 60 -
               kUtcOffsetSeconds;
  plan.kind = kSlots[0].kind;
  return plan;
}

}  // namespace app
