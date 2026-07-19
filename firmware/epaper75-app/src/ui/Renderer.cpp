#include "ui/Renderer.h"

#include <GxEPD2.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board/BoardConfig.h"

namespace ui {
namespace {

constexpr uint16_t kWhite = GxEPD_WHITE;
constexpr uint16_t kBlack = GxEPD_BLACK;
constexpr uint16_t kRed = GxEPD_RED;
constexpr int16_t kStatusBottom = 62;
constexpr int32_t kBeijingUtcOffsetSeconds = 8 * 60 * 60;
constexpr int16_t kHomeDividerX = 340;
constexpr int16_t kDeviceLeft = 364;
constexpr int16_t kContentRight = 776;

void text(Adafruit_GFX& canvas, int16_t x, int16_t y, uint8_t size,
          uint16_t color, const char* value) {
  canvas.setTextWrap(false);
  canvas.setTextSize(size);
  canvas.setTextColor(color);
  canvas.setCursor(x, y);
  canvas.print(value != nullptr ? value : "");
}

void rightText(Adafruit_GFX& canvas, int16_t right, int16_t y, uint8_t size,
               uint16_t color, const char* value) {
  int16_t boundsX = 0;
  int16_t boundsY = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  canvas.setTextWrap(false);
  canvas.setTextSize(size);
  canvas.getTextBounds(value != nullptr ? value : "", 0, 0, &boundsX,
                       &boundsY, &width, &height);
  text(canvas, static_cast<int16_t>(right - width - boundsX), y, size, color,
       value);
}

void boldLine(Adafruit_GFX& canvas, int16_t x0, int16_t y0, int16_t x1,
              int16_t y1, uint16_t color) {
  canvas.drawLine(x0, y0, x1, y1, color);
  canvas.drawLine(x0, y0 + 1, x1, y1 + 1, color);
}

void drawWifiIcon(Adafruit_GFX& canvas, int16_t centerX, int16_t baselineY,
                  uint16_t color) {
  // Concentric upper semicircles produce a cleaner standard Wi-Fi glyph than
  // the previous chevron approximation. The white mask removes lower halves.
  canvas.drawCircle(centerX, baselineY, 16, color);
  canvas.drawCircle(centerX, baselineY, 15, color);
  canvas.drawCircle(centerX, baselineY, 10, color);
  canvas.drawCircle(centerX, baselineY, 9, color);
  canvas.drawCircle(centerX, baselineY, 5, color);
  canvas.drawCircle(centerX, baselineY, 4, color);
  canvas.fillRect(centerX - 18, baselineY, 37, 18, kWhite);
  canvas.fillCircle(centerX, baselineY, 3, color);
}

// Converts externally supplied UTF-8 into a bounded, printable ASCII label.
// One '?' is emitted per non-ASCII code point so malformed or multilingual
// task names cannot disturb the fixed 800x480 layout.
void printableCopy(char* destination, size_t capacity, const char* source,
                   size_t maxCharacters, bool uppercase = false) {
  if (capacity == 0) return;
  if (source == nullptr) source = "";
  if (maxCharacters + 1U > capacity) maxCharacters = capacity - 1U;

  size_t output = 0;
  bool truncated = false;
  while (*source != '\0') {
    if (output >= maxCharacters) {
      truncated = true;
      break;
    }

    const unsigned char value = static_cast<unsigned char>(*source++);
    char printable = ' ';
    if (value >= 0x20U && value <= 0x7eU) {
      printable = static_cast<char>(value);
    } else if (value >= 0x80U) {
      printable = '?';
      while ((static_cast<unsigned char>(*source) & 0xc0U) == 0x80U) {
        ++source;
      }
    }
    if (uppercase) {
      printable = static_cast<char>(
          toupper(static_cast<unsigned char>(printable)));
    }
    destination[output++] = printable;
  }

  if (truncated && maxCharacters >= 3U) {
    destination[maxCharacters - 3U] = '.';
    destination[maxCharacters - 2U] = '.';
    destination[maxCharacters - 1U] = '.';
    output = maxCharacters;
  }
  destination[output] = '\0';
}

const char* availabilityLabel(app::DataAvailability availability,
                              bool stale) {
  if (stale) return "STALE";
  switch (availability) {
    case app::DataAvailability::Available:
      return "CURRENT";
    case app::DataAvailability::Unavailable:
      return "UNAVAILABLE";
    case app::DataAvailability::Unknown:
    default:
      return "WAITING";
  }
}

bool availabilityAlert(app::DataAvailability availability, bool stale) {
  return stale || availability == app::DataAvailability::Unavailable;
}

const char* workloadLabel(const app::AgentDeviceSnapshot& device) {
  switch (device.reachability) {
    case app::AgentReachability::Offline:
      return "OFFLINE";
    case app::AgentReachability::AuthError:
      return "AUTH ERROR";
    case app::AgentReachability::ProtocolError:
      return "BAD DATA";
    case app::AgentReachability::Connecting:
      return "CONNECTING";
    case app::AgentReachability::Recovering:
      return "RECOVERING";
    case app::AgentReachability::Unconfigured:
      return "NOT SET";
    case app::AgentReachability::Online:
      break;
  }
  if (device.stale) return "STALE";

  switch (device.workload) {
    case app::WorkloadState::Running:
      return "RUNNING";
    case app::WorkloadState::WaitingApproval:
      return "APPROVAL";
    case app::WorkloadState::WaitingInput:
      return "INPUT";
    case app::WorkloadState::Idle:
      return "IDLE";
    case app::WorkloadState::Error:
      return "ERROR";
    case app::WorkloadState::Unknown:
    default:
      return "UNKNOWN";
  }
}

bool deviceAlert(const app::AgentDeviceSnapshot& device) {
  return device.stale || app::deviceNeedsAction(device) ||
         device.reachability == app::AgentReachability::Offline ||
         device.reachability == app::AgentReachability::Unconfigured ||
         device.reachability == app::AgentReachability::AuthError ||
         device.reachability == app::AgentReachability::ProtocolError ||
         device.workload == app::WorkloadState::Error || device.errors > 0;
}

void formatActivityAge(char* output, size_t capacity,
                       const app::AgentDeviceSnapshot& device) {
  if (device.receivedAtMs == 0U ||
      device.reachability == app::AgentReachability::Unconfigured ||
      device.activityAgeSecondsAtReceive ==
          app::kUnknownActivityAgeSeconds) {
    snprintf(output, capacity, "NO DATA");
    return;
  }
  const uint32_t age = device.activityAgeSecondsAtReceive;
  if (age < 15U) {
    snprintf(output, capacity, "LIVE");
  } else if (age < 60U) {
    snprintf(output, capacity, "UPDATED %lus",
             static_cast<unsigned long>(age));
  } else if (age < 3600U) {
    snprintf(output, capacity, "UPDATED %lum",
             static_cast<unsigned long>(age / 60U));
  } else if (age < 86400U) {
    snprintf(output, capacity, "UPDATED %luh",
             static_cast<unsigned long>(age / 3600U));
  } else {
    snprintf(output, capacity, "UPDATED %lud",
             static_cast<unsigned long>(age / 86400U));
  }
}

void formatResetAt(char* output, size_t capacity, int64_t resetTimestamp) {
  // The Agent rate-limit contract is minute-precision on screen.
  if (resetTimestamp < 946684800LL) {
    snprintf(output, capacity, "--");
    return;
  }
  const time_t beijingTime =
      static_cast<time_t>(resetTimestamp + kBeijingUtcOffsetSeconds);
  struct tm brokenDown = {};
  if (gmtime_r(&beijingTime, &brokenDown) == nullptr) {
    snprintf(output, capacity, "--");
    return;
  }
  snprintf(output, capacity, "%04d-%02d-%02d %02d:%02d",
           brokenDown.tm_year + 1900, brokenDown.tm_mon + 1,
           brokenDown.tm_mday, brokenDown.tm_hour, brokenDown.tm_min);
}

uint16_t alertColor(bool alert) { return alert ? kRed : kBlack; }

}  // namespace

void Renderer::render(Adafruit_GFX& canvas, const app::UiModel& model) const {
  canvas.fillScreen(kWhite);
  drawStatusBar(canvas, model);
  if ((model.page % 2U) == 0U) {
    drawDashboard(canvas, model);
  } else {
    drawSystemPage(canvas, model);
  }
}

void Renderer::drawStatusBar(Adafruit_GFX& canvas,
                             const app::UiModel& model) const {
  char line[12];

  canvas.fillRect(24, 14, 6, 32, kRed);
  text(canvas, 42, 12, 3, kBlack, "CODEX FLEET");

  drawWifiIcon(canvas, 698, 38,
               alertColor(!model.network.connected));
  const uint8_t online = app::fleetOnlineCount(model.fleet);
  snprintf(line, sizeof(line), "%u/%u", online,
           static_cast<unsigned>(app::kAgentDeviceCount));
  rightText(canvas, kContentRight, 18, 2,
            alertColor(online < app::kAgentDeviceCount), line);

  canvas.drawFastHLine(20, kStatusBottom, 760, kBlack);
}

void Renderer::drawDashboard(Adafruit_GFX& canvas,
                             const app::UiModel& model) const {
  canvas.drawFastVLine(kHomeDividerX, 84, 356, kBlack);
  drawAccountQuota(canvas, model);

  char summary[36];
  snprintf(summary, sizeof(summary), "%u ONLINE / %u ACTION",
           app::fleetOnlineCount(model.fleet),
           app::fleetActionCount(model.fleet));
  text(canvas, kDeviceLeft, 82, 2, kBlack, "DEVICES");
  rightText(canvas, kContentRight, 88, 1,
            alertColor(app::fleetActionCount(model.fleet) > 0), summary);
  boldLine(canvas, kDeviceLeft, 112, kContentRight, 112, kBlack);

  drawDeviceRow(canvas, model, 0, 124);
  drawDeviceRow(canvas, model, 1, 225);
  drawDeviceRow(canvas, model, 2, 326);
}

void Renderer::drawAccountQuota(Adafruit_GFX& canvas,
                                const app::UiModel& model) const {
  constexpr int16_t x = 24;
  constexpr int16_t width = 292;
  const app::AccountQuotaSnapshot& account = model.fleet.account;
  const int16_t weeklyRemaining =
      app::accountWeeklyRemainingPercent(account);
  const bool lowQuota = weeklyRemaining >= 0 && weeklyRemaining <= 15;
  const bool dataAlert = availabilityAlert(account.availability, account.stale);

  char line[48];
  char resetAt[32];
  char source[24];

  text(canvas, x, 82, 2, kBlack, "ACCOUNT");
  rightText(canvas, x + width, 88, 1, kBlack, "SHARED CODEX");
  text(canvas, x, 124, 2, kBlack, "WEEKLY REMAINING");
  if (weeklyRemaining >= 0)
    snprintf(line, sizeof(line), "%d%%", weeklyRemaining);
  else
    snprintf(line, sizeof(line), "--%%");
  text(canvas, x, 154, 6, alertColor(lowQuota), line);
  drawProgress(canvas, x, 215, width, weeklyRemaining, lowQuota);

  text(canvas, x, 255, 1, kBlack, "RESET AT");
  formatResetAt(resetAt, sizeof(resetAt), account.weeklyResetTimestamp);
  text(canvas, x, 274, 2, kBlack, resetAt);
  rightText(canvas, x + width, 306, 1, kBlack, "UTC+8");
  canvas.drawFastHLine(x, 330, width, kBlack);

  text(canvas, x, 350, 1, kBlack, "PRIMARY SOURCE");
  if (account.sourceIndex < app::kAgentDeviceCount) {
    printableCopy(source, sizeof(source),
                  model.fleet.devices[account.sourceIndex].label, 18, true);
  } else {
    snprintf(source, sizeof(source), "--");
  }
  rightText(canvas, x + width, 345, 2, kBlack, source);

  text(canvas, x, 385, 1, kBlack, "ACCOUNT DATA");
  const char* availability =
      availabilityLabel(account.availability, account.stale);
  rightText(canvas, x + width, 380, 2, alertColor(dataAlert), availability);

  text(canvas, x, 420, 1, kBlack, "UPDATED");
  if (account.sourceGeneratedTimestamp > 0) {
    formatResetAt(resetAt, sizeof(resetAt),
                  account.sourceGeneratedTimestamp);
    rightText(canvas, x + width, 420, 1, alertColor(account.stale),
              resetAt + 11);
  } else {
    rightText(canvas, x + width, 420, 1, alertColor(account.stale), "--");
  }
}

void Renderer::drawDeviceRow(Adafruit_GFX& canvas,
                             const app::UiModel& model, size_t deviceIndex,
                             int16_t y) const {
  if (deviceIndex >= app::kAgentDeviceCount) return;
  const app::AgentDeviceSnapshot& device = model.fleet.devices[deviceIndex];
  const bool alert = deviceAlert(device);
  const bool primary = model.fleet.account.sourceIndex == deviceIndex;

  char label[24];
  char task[36];
  char counts[52];
  char age[24];
  printableCopy(label, sizeof(label), device.label, 18, true);
  printableCopy(task, sizeof(task), device.currentTask, 32);
  if (label[0] == '\0') snprintf(label, sizeof(label), "DEVICE %u",
                                 static_cast<unsigned>(deviceIndex + 1U));
  if (task[0] == '\0') snprintf(task, sizeof(task), "No active task");

  if (alert) canvas.fillRect(kDeviceLeft, y, 5, 76, kRed);
  const int16_t contentLeft = static_cast<int16_t>(kDeviceLeft +
                                                   (alert ? 16 : 0));
  text(canvas, contentLeft, y, 2, kBlack, label);
  if (primary) text(canvas, contentLeft + 136, y + 6, 1, kBlack, "PRIMARY");

  const char* state = workloadLabel(device);
  rightText(canvas, kContentRight, y, 2, alertColor(alert), state);
  text(canvas, contentLeft, y + 31, 2, kBlack, task);

  const int16_t waiting =
      device.waitingApproval >= 0 && device.waitingInput >= 0
          ? static_cast<int16_t>(device.waitingApproval +
                                 device.waitingInput)
          : -1;
  char workflows[8];
  char workers[8];
  if (device.activeWorkflows >= 0) {
    snprintf(workflows, sizeof(workflows), "%d", device.activeWorkflows);
  } else {
    snprintf(workflows, sizeof(workflows), "--");
  }
  if (device.activeWorkers >= 0) {
    snprintf(workers, sizeof(workers), "%d", device.activeWorkers);
  } else {
    snprintf(workers, sizeof(workers), "--");
  }
  if (waiting > 0) {
    snprintf(counts, sizeof(counts), "%s FLOW / %s WORKERS / %d WAITING",
             workflows, workers, waiting);
  } else if (device.errors > 0) {
    snprintf(counts, sizeof(counts), "%s FLOW / %s WORKERS / %d ERROR",
             workflows, workers, device.errors);
  } else if (device.running >= 0) {
    snprintf(counts, sizeof(counts), "%s FLOW / %s WORKERS / %d RUN",
             workflows, workers, device.running);
  } else {
    snprintf(counts, sizeof(counts), "%s FLOW / %s WORKERS",
             workflows, workers);
  }
  text(canvas, contentLeft, y + 59, 1, kBlack, counts);
  if (app::deviceNeedsAction(device)) {
    rightText(canvas, kContentRight, y + 59, 1, kRed, "ACTION NEEDED");
  } else {
    formatActivityAge(age, sizeof(age), device);
    rightText(canvas, kContentRight, y + 59, 1,
              alertColor(device.stale), age);
  }

  if (deviceIndex + 1U < app::kAgentDeviceCount) {
    canvas.drawFastHLine(kDeviceLeft, y + 89,
                         kContentRight - kDeviceLeft, kBlack);
  }
}

void Renderer::drawProgress(Adafruit_GFX& canvas, int16_t x, int16_t y,
                            int16_t width, int16_t percent,
                            bool critical) const {
  constexpr int16_t height = 18;
  canvas.drawRect(x, y, width, height, kBlack);
  if (percent < 0) return;
  int16_t bounded = percent;
  if (bounded > 100) bounded = 100;
  const int16_t filled = static_cast<int16_t>(
      (static_cast<int32_t>(width - 4) * bounded) / 100);
  if (filled > 0) {
    canvas.fillRect(x + 2, y + 2, filled, height - 4,
                    critical ? kRed : kBlack);
  }
}

void Renderer::drawSystemPage(Adafruit_GFX& canvas,
                              const app::UiModel& model) const {
  char line[128];
  char source[24];
  constexpr int16_t left = 24;
  constexpr int16_t right = 410;

  text(canvas, left, 77, 2, kBlack, "CONNECTIVITY");
  canvas.drawFastHLine(left, 100, 350, kBlack);
  snprintf(line, sizeof(line), "WIFI      %s",
           model.network.connected ? "ONLINE" : "OFFLINE");
  text(canvas, left, 113, 2, alertColor(!model.network.connected), line);
  if (model.network.connected) {
    snprintf(line, sizeof(line), "SIGNAL    %d DBM", model.network.rssi);
  } else {
    snprintf(line, sizeof(line), "SIGNAL    -- DBM");
  }
  text(canvas, left, 140, 2, kBlack, line);
  const uint8_t online = app::fleetOnlineCount(model.fleet);
  snprintf(line, sizeof(line), "AGENTS    %u/%u ONLINE", online,
           static_cast<unsigned>(app::kAgentDeviceCount));
  text(canvas, left, 167, 2,
       alertColor(online < app::kAgentDeviceCount), line);
  const uint8_t actions = app::fleetActionCount(model.fleet);
  snprintf(line, sizeof(line), "ACTIONS   %u", actions);
  text(canvas, left, 194, 2, alertColor(actions > 0), line);

  text(canvas, left, 254, 2, kBlack, "FLEET DATA");
  canvas.drawFastHLine(left, 277, 350, kBlack);
  const bool accountAlert = availabilityAlert(
      model.fleet.account.availability, model.fleet.account.stale);
  snprintf(line, sizeof(line), "ACCOUNT   %s",
           availabilityLabel(model.fleet.account.availability,
                             model.fleet.account.stale));
  text(canvas, left, 290, 2, alertColor(accountAlert), line);
  if (model.fleet.account.sourceIndex < app::kAgentDeviceCount) {
    printableCopy(source, sizeof(source),
                  model.fleet.devices[model.fleet.account.sourceIndex].label,
                  18, true);
  } else {
    snprintf(source, sizeof(source), "--");
  }
  snprintf(line, sizeof(line), "SOURCE    %s", source);
  text(canvas, left, 317, 2, kBlack, line);

  uint8_t verified = 0;
  uint8_t exact = 0;
  for (size_t i = 0; i < app::kAgentDeviceCount; ++i) {
    if (model.fleet.devices[i].identityVerified) ++verified;
    if (model.fleet.devices[i].stateExact) ++exact;
  }
  snprintf(line, sizeof(line), "IDENTITY  %u/%u VERIFIED", verified,
           static_cast<unsigned>(app::kAgentDeviceCount));
  text(canvas, left, 344, 2,
       alertColor(verified < app::kAgentDeviceCount), line);
  snprintf(line, sizeof(line), "STATE     %u/%u EXACT", exact,
           static_cast<unsigned>(app::kAgentDeviceCount));
  text(canvas, left, 371, 2, kBlack, line);

  text(canvas, right, 77, 2, kBlack, "DEVICE");
  canvas.drawFastHLine(right, 100, 366, kBlack);
  snprintf(line, sizeof(line), "FIRMWARE  %s", board::kFirmwareVersion);
  text(canvas, right, 113, 2, kBlack, line);
  text(canvas, right, 140, 2, kBlack, "MCU       ESP32-WROOM-32E");
  text(canvas, right, 167, 2, kBlack, "PANEL     800x480 B/W/RED");
  text(canvas, right, 194, 2, kBlack, "FLASH     8 MB / NO PSRAM");
  text(canvas, right, 221, 1, kBlack, "DISPLAY   PARTIAL + FULL CLEAN");

  text(canvas, right, 254, 2, kBlack, "SENSORS & BATTERY");
  canvas.drawFastHLine(right, 277, 366, kBlack);
  if (model.telemetry.sensorOk) {
    snprintf(line, sizeof(line), "TEMP      %.1f C",
             model.telemetry.temperatureC);
    text(canvas, right, 290, 2, kBlack, line);
    snprintf(line, sizeof(line), "HUMIDITY  %.0f %%",
             model.telemetry.humidityPercent);
    text(canvas, right, 317, 2, kBlack, line);
  } else {
    text(canvas, right, 290, 2, kRed, "SHT30     OFFLINE");
    text(canvas, right, 317, 2, kBlack, "HUMIDITY  -- %");
  }
  if (model.telemetry.batteryPercent >= 0) {
    snprintf(line, sizeof(line), "BATTERY   ~%d %%",
             model.telemetry.batteryPercent);
  } else {
    snprintf(line, sizeof(line), "BATTERY   -- %%");
  }
  const bool lowBattery = model.telemetry.batteryPercent >= 0 &&
                          model.telemetry.batteryPercent <= 10;
  text(canvas, right, 344, 2, alertColor(lowBattery), line);
  if (model.telemetry.batteryMillivolts >= 500) {
    snprintf(line, sizeof(line), "RAW ADC EST.  %.2f V",
             model.telemetry.batteryMillivolts / 1000.0f);
  } else {
    snprintf(line, sizeof(line), "RAW ADC EST.  --.-- V");
  }
  text(canvas, right, 371, 1, kBlack, line);
}

}  // namespace ui
