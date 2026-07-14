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

constexpr const char* kLatencyLabels[app::kLatencyTargetCount] = {
    "NETEASE MUSIC", "GITHUB", "GOOGLE", "YOUTUBE"};
constexpr const char* kLatencyHosts[app::kLatencyTargetCount] = {
    "music.163.com", "github.com", "google.com", "youtube.com"};

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

void drawBridgeIcon(Adafruit_GFX& canvas, int16_t centerX, int16_t centerY,
                    uint16_t color) {
  // Two offset capsule outlines form a simple chain-link symbol.
  canvas.drawRoundRect(centerX - 19, centerY - 9, 24, 14, 7, color);
  canvas.drawRoundRect(centerX - 18, centerY - 8, 22, 12, 6, color);
  canvas.drawRoundRect(centerX - 4, centerY - 5, 24, 14, 7, color);
  canvas.drawRoundRect(centerX - 3, centerY - 4, 22, 12, 6, color);
  boldLine(canvas, centerX - 7, centerY + 3, centerX + 8, centerY - 3,
           color);
}

void drawBatteryIcon(Adafruit_GFX& canvas, int16_t x, int16_t y,
                     int8_t percent, uint16_t color) {
  constexpr int16_t bodyWidth = 34;
  constexpr int16_t bodyHeight = 18;
  canvas.drawRect(x, y, bodyWidth, bodyHeight, color);
  canvas.drawRect(x + 1, y + 1, bodyWidth - 2, bodyHeight - 2, color);
  canvas.fillRect(x + bodyWidth, y + 5, 4, 8, color);
  if (percent < 0) return;
  const int16_t bounded = percent > 100 ? 100 : percent;
  const int16_t fillWidth =
      static_cast<int16_t>((static_cast<int32_t>(bodyWidth - 6) * bounded) /
                           100);
  if (fillWidth > 0) {
    canvas.fillRect(x + 3, y + 3, fillWidth, bodyHeight - 6, color);
  }
}

void uppercaseCopy(char* destination, size_t capacity, const char* source) {
  if (capacity == 0) return;
  if (source == nullptr) source = "";
  size_t i = 0;
  for (; i + 1 < capacity && source[i] != '\0'; ++i) {
    destination[i] = static_cast<char>(
        toupper(static_cast<unsigned char>(source[i])));
  }
  destination[i] = '\0';
}

bool sameIgnoreCase(const char* lhs, const char* rhs) {
  return lhs != nullptr && rhs != nullptr && strcasecmp(lhs, rhs) == 0;
}

bool criticalStatus(const char* status) {
  return sameIgnoreCase(status, "offline") ||
         sameIgnoreCase(status, "critical") ||
         sameIgnoreCase(status, "error") || sameIgnoreCase(status, "failed");
}

void formatTokens(char* output, size_t capacity, int32_t value) {
  if (value < 0) {
    snprintf(output, capacity, "--");
  } else if (value >= 1000000) {
    snprintf(output, capacity, "%.1fM", value / 1000000.0f);
  } else if (value >= 1000) {
    snprintf(output, capacity, "%.1fK", value / 1000.0f);
  } else {
    snprintf(output, capacity, "%ld", static_cast<long>(value));
  }
}

int16_t remainingPercent(int16_t usedPercent) {
  if (usedPercent < 0) return -1;
  if (usedPercent > 100) usedPercent = 100;
  return static_cast<int16_t>(100 - usedPercent);
}

void formatResetAt(char* output, size_t capacity, int64_t resetTimestamp) {
  // The Bridge contract is minute-precision, so the screen omits seconds.
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
  char line[24];

  canvas.fillRect(24, 14, 6, 32, kRed);
  text(canvas, 42, 12, 3, kBlack,
       (model.page % 2U) == 0U ? "CODEX MONITOR" : "SYSTEM & CONFIG");

  const bool batteryCritical = model.telemetry.batteryPercent >= 0 &&
                               model.telemetry.batteryPercent <= 10;
  const uint16_t batteryColor = alertColor(batteryCritical);
  drawBatteryIcon(canvas, 512, 22, model.telemetry.batteryPercent,
                  batteryColor);
  if (model.telemetry.batteryPercent >= 0) {
    snprintf(line, sizeof(line), "~%d%%", model.telemetry.batteryPercent);
  } else {
    snprintf(line, sizeof(line), "--%%");
  }
  text(canvas, 558, 18, 2, batteryColor, line);

  drawWifiIcon(canvas, 667, 38,
               alertColor(!model.network.connected));
  drawBridgeIcon(canvas, 748, 29,
                 alertColor(!model.bridge.reachable));

  canvas.drawFastHLine(20, kStatusBottom, 760, kBlack);
}

void Renderer::drawDashboard(Adafruit_GFX& canvas,
                             const app::UiModel& model) const {
  canvas.drawFastVLine(400, 84, 355, kBlack);
  drawCodexUsage(canvas, model);

  text(canvas, 424, 82, 2, kBlack, "NETWORK");
  rightText(canvas, 776, 90, 1, kBlack, "TCP/443");
  text(canvas, 424, 126, 1, kBlack, "SERVICE");
  rightText(canvas, 776, 126, 1, kBlack, "LATENCY");
  canvas.drawFastHLine(424, 148, 352, kBlack);
  canvas.drawFastHLine(424, 149, 352, kBlack);
  for (size_t i = 0; i < app::kLatencyTargetCount; ++i) {
    drawLatencyRow(canvas, model.networkQuality.targets[i], i, 424,
                   static_cast<int16_t>(158 + i * 68), 352);
  }
}

void Renderer::drawCodexUsage(Adafruit_GFX& canvas,
                              const app::UiModel& model) const {
  constexpr int16_t x = 24;
  constexpr int16_t width = 352;
  const app::AgentSnapshot& codex = model.bridge.codex;
  const int16_t fiveHourRemaining = remainingPercent(codex.primaryPercent);
  const int16_t weeklyRemaining = remainingPercent(codex.secondaryPercent);
  const bool statusCritical = !model.bridge.reachable || codex.needsInput ||
                              criticalStatus(codex.status);

  char line[96];
  char status[app::kStatusTextSize];
  char value[32];
  char resetAt[32];

  text(canvas, x, 82, 2, kBlack, "QUOTA");
  const char* statusText = codex.status;
  if (!model.bridge.reachable) {
    statusText = "BRIDGE OFFLINE";
  } else if (codex.needsInput) {
    statusText = "NEEDS INPUT";
  } else if (criticalStatus(codex.status)) {
    statusText = "CODEX ERROR";
  }
  uppercaseCopy(status, sizeof(status), statusText);
  int16_t statusX = 0;
  int16_t statusY = 0;
  uint16_t statusWidth = 0;
  uint16_t statusHeight = 0;
  canvas.setTextSize(2);
  canvas.getTextBounds(status, 0, 0, &statusX, &statusY, &statusWidth,
                       &statusHeight);
  const int16_t statusLeft =
      static_cast<int16_t>(x + width - statusWidth - statusX);
  canvas.fillCircle(statusLeft - 12, 92, 4,
                    alertColor(statusCritical));
  text(canvas, statusLeft, 83, 2, alertColor(statusCritical), status);

  formatTokens(value, sizeof(value), codex.tokensToday);
  text(canvas, x, 123, 1, kBlack, "TODAY");
  snprintf(line, sizeof(line), "%s TOKENS", value);
  text(canvas, x + 64, 117, 2, kBlack, line);
  canvas.drawFastHLine(x, 151, width, kBlack);

  text(canvas, x, 169, 2, kBlack, "5-HOUR REMAINING");
  if (fiveHourRemaining >= 0)
    snprintf(line, sizeof(line), "%d%%", fiveHourRemaining);
  else
    snprintf(line, sizeof(line), "--%%");
  rightText(canvas, x + width, 154, 5,
            alertColor(fiveHourRemaining >= 0 && fiveHourRemaining <= 15),
            line);
  drawProgress(canvas, x, 215, width, fiveHourRemaining,
               fiveHourRemaining >= 0 && fiveHourRemaining <= 15);
  formatResetAt(resetAt, sizeof(resetAt), codex.primaryResetTimestamp);
  text(canvas, x, 244, 1, kBlack, "RESET");
  text(canvas, x + 58, 238, 2, kBlack, resetAt);
  text(canvas, x + 271, 244, 1, kBlack, "UTC+8");
  canvas.drawFastHLine(x, 276, width, kBlack);

  text(canvas, x, 294, 2, kBlack, "WEEKLY REMAINING");
  if (weeklyRemaining >= 0)
    snprintf(line, sizeof(line), "%d%%", weeklyRemaining);
  else
    snprintf(line, sizeof(line), "--%%");
  rightText(canvas, x + width, 279, 5,
            alertColor(weeklyRemaining >= 0 && weeklyRemaining <= 15), line);
  drawProgress(canvas, x, 340, width, weeklyRemaining,
               weeklyRemaining >= 0 && weeklyRemaining <= 15);
  formatResetAt(resetAt, sizeof(resetAt), codex.secondaryResetTimestamp);
  text(canvas, x, 369, 1, kBlack, "RESET");
  text(canvas, x + 58, 363, 2, kBlack, resetAt);
  text(canvas, x + 271, 369, 1, kBlack, "UTC+8");
}

void Renderer::drawLatencyRow(Adafruit_GFX& canvas,
                              const app::LatencyTargetSnapshot& target,
                              size_t targetIndex, int16_t x, int16_t y,
                              int16_t width) const {
  char latency[24];
  const char* quality = "MEASURING";
  bool alert = false;

  if (target.sampledAtMs == 0) {
    snprintf(latency, sizeof(latency), "-- ms");
  } else if (!target.reachable || target.latencyMs < 0) {
    snprintf(latency, sizeof(latency), "OFFLINE");
    quality = "NO TCP CONNECTION";
    alert = true;
  } else {
    snprintf(latency, sizeof(latency), "%ld ms",
             static_cast<long>(target.latencyMs));
    if (target.latencyMs <= 100) {
      quality = "GOOD";
    } else if (target.latencyMs <= 300) {
      quality = "OK";
    } else if (target.latencyMs <= 700) {
      quality = "SLOW";
      alert = true;
    } else {
      quality = "VERY SLOW";
      alert = true;
    }
  }

  const size_t boundedIndex =
      targetIndex < app::kLatencyTargetCount ? targetIndex : 0;
  text(canvas, x, y, 2, kBlack, kLatencyLabels[boundedIndex]);
  rightText(canvas, x + width, y - 2, 3, alertColor(alert), latency);
  text(canvas, x, y + 28, 1, kBlack, kLatencyHosts[boundedIndex]);
  rightText(canvas, x + width, y + 30, 1, alertColor(alert), quality);
  if (boundedIndex + 1U < app::kLatencyTargetCount) {
    canvas.drawFastHLine(x, y + 58, width, kBlack);
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
  constexpr int16_t left = 24;
  constexpr int16_t right = 410;

  text(canvas, left, 77, 2, kBlack, "NETWORK");
  canvas.drawFastHLine(left, 100, 350, kBlack);
  snprintf(line, sizeof(line), "WIFI      %s",
           model.network.connected ? "ONLINE" : "OFFLINE");
  text(canvas, left, 113, 2, alertColor(!model.network.connected), line);
  snprintf(line, sizeof(line), "SSID      %.18s",
           model.network.ssid[0] ? model.network.ssid : "--");
  text(canvas, left, 140, 2, kBlack, line);
  snprintf(line, sizeof(line), "IP        %s", model.network.ip);
  text(canvas, left, 167, 2, kBlack, line);
  snprintf(line, sizeof(line), "RSSI      %d DBM", model.network.rssi);
  text(canvas, left, 194, 2, kBlack, line);

  text(canvas, left, 254, 2, kBlack, "BRIDGE");
  canvas.drawFastHLine(left, 277, 350, kBlack);
  snprintf(line, sizeof(line), "STATE     %s",
           model.bridge.reachable ? "ONLINE" : "OFFLINE");
  text(canvas, left, 290, 2, alertColor(!model.bridge.reachable), line);
  snprintf(line, sizeof(line), "HOST      %.18s",
           model.endpoint.configured() ? model.endpoint.host : "NOT SET");
  text(canvas, left, 317, 2,
       alertColor(!model.endpoint.configured()), line);
  snprintf(line, sizeof(line), "PORT      %u", model.endpoint.port);
  text(canvas, left, 344, 2, kBlack, line);

  text(canvas, right, 77, 2, kBlack, "DEVICE");
  canvas.drawFastHLine(right, 100, 366, kBlack);
  snprintf(line, sizeof(line), "FIRMWARE  %s", board::kFirmwareVersion);
  text(canvas, right, 113, 2, kBlack, line);
  text(canvas, right, 140, 2, kBlack, "MCU       ESP32-WROOM-32E");
  text(canvas, right, 167, 2, kBlack, "PANEL     800x480 B/W/RED");
  text(canvas, right, 194, 2, kBlack, "SETUP AP  Epaper75-Setup");
  text(canvas, right, 221, 1, kBlack, "REFRESH   FULL / EVENT DRIVEN");

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
