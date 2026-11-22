#include "ui/Renderer.h"

#include <GxEPD2.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board/BoardConfig.h"
#include "cal/Calendar.h"
#include "ui/Fonts.h"
#include "ui/Icons.h"
#include "weather/WeatherText.h"

namespace ui {
namespace {

constexpr uint16_t kWhite = GxEPD_WHITE;
constexpr uint16_t kBlack = GxEPD_BLACK;
constexpr uint16_t kRed = GxEPD_RED;
constexpr int32_t kUtcOffsetSeconds = 8 * 60 * 60;

constexpr int16_t kLeft = 24;
constexpr int16_t kRight = 776;
constexpr int16_t kPanelRight = 376;   // end of the left column
constexpr int16_t kCalendarLeft = 412;
constexpr int16_t kCalendarCell = 52;

const char* const kMonthEnglish[12] = {
    "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
    "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
const char* const kWeekShort[7] = {"周一", "周二", "周三", "周四",
                                   "周五", "周六", "周日"};

struct LocalTime {
  cal::Date date;
  int16_t minute = 0;  // minutes after local midnight
};

LocalTime toLocal(int64_t epoch) {
  const int64_t local = epoch + kUtcOffsetSeconds;
  int64_t days = local / 86400;
  int64_t rest = local % 86400;
  if (rest < 0) {
    rest += 86400;
    --days;
  }
  LocalTime out;
  out.date = cal::civilFromDays(static_cast<int32_t>(days));
  out.minute = static_cast<int16_t>(rest / 60);
  return out;
}

void hline(Adafruit_GFX& canvas, int16_t x0, int16_t x1, int16_t y,
           int16_t width = 1) {
  canvas.fillRect(x0, y, x1 - x0, width, kBlack);
}

void vline(Adafruit_GFX& canvas, int16_t x, int16_t y0, int16_t y1) {
  canvas.drawFastVLine(x, y0, y1 - y0, kBlack);
}

void formatTemperature(char* out, size_t capacity, int16_t value) {
  if (value == app::kNoValue) {
    snprintf(out, capacity, "--");
  } else {
    snprintf(out, capacity, "%d", value);
  }
}

void formatClock(char* out, size_t capacity, int16_t minute) {
  if (minute < 0) {
    snprintf(out, capacity, "--:--");
  } else {
    snprintf(out, capacity, "%02d:%02d", minute / 60, minute % 60);
  }
}

// Red chip with white text; returns its right edge.
int16_t chip(Adafruit_GFX& canvas, int16_t x, int16_t top, const char* text,
             uint16_t fill, uint16_t ink) {
  const int16_t width = static_cast<int16_t>(textWidth(kFontSmallBold, text) + 14);
  canvas.fillRect(x, top, width, 22, fill);
  drawTextAligned(canvas, kFontSmallBold, x + width / 2, top + 16, text, ink,
                  Align::Center);
  return static_cast<int16_t>(x + width);
}

const char* errorText(app::WeatherError error) {
  switch (error) {
    case app::WeatherError::None: return "正常";
    case app::WeatherError::NotConfigured: return "未配置";
    case app::WeatherError::NoWifi: return "无网络";
    case app::WeatherError::ClockNotSynced: return "未校时";
    case app::WeatherError::Tls: return "TLS 失败";
    case app::WeatherError::Http: return "HTTP 错误";
    case app::WeatherError::Unauthorized: return "认证失败";
    case app::WeatherError::RateLimited: return "超出限额";
    case app::WeatherError::Decompress: return "解压失败";
    case app::WeatherError::Parse: return "解析失败";
    case app::WeatherError::LowBattery: return "电量低";
    default: return "未知";
  }
}

bool lowBattery(const app::TelemetrySnapshot& telemetry) {
  return telemetry.batteryPercent >= 0 && telemetry.batteryPercent <= 15;
}

}  // namespace

void Renderer::render(Adafruit_GFX& canvas, const app::UiModel& model) const {
  canvas.fillScreen(kWhite);
  if (model.page == app::Page::System) {
    drawSystemPage(canvas, model);
    return;
  }

  const char* title = model.place.valid ? model.place.name : "天气日历";
  const char* subtitle = model.place.valid ? model.place.city : "";
  drawHeader(canvas, model, title, subtitle);
  drawToday(canvas, model);
  drawCurrentWeather(canvas, model);
  vline(canvas, 394, 80, 376);
  drawCalendar(canvas, model);
  drawForecast(canvas, model);
}

void Renderer::drawHeader(Adafruit_GFX& canvas, const app::UiModel& model,
                          const char* title, const char* subtitle) const {
  canvas.fillRect(kLeft, 18, 8, 30, kRed);
  int16_t x = drawText(canvas, kFontTitle, 44, 45, title, kBlack);
  if (subtitle != nullptr && subtitle[0] != '\0' &&
      strcmp(subtitle, title) != 0) {
    drawText(canvas, kFontBody, x + 12, 45, subtitle, kBlack);
  }

  const app::WeatherSnapshot& weather = model.weather;
  int16_t right = kRight;

  // Connection glyph reflects the most recent fetch attempt.
  const bool fetchFailed =
      weather.availability == app::DataAvailability::Unavailable;
  drawWifiIcon(canvas, right - 14, 44, fetchFailed ? kRed : kBlack, kWhite,
               fetchFailed);
  right = static_cast<int16_t>(right - 40);

  if (lowBattery(model.telemetry)) {
    drawBatteryIcon(canvas, right - 32, 30, model.telemetry.batteryPercent,
                    kRed, kWhite);
    right = static_cast<int16_t>(right - 44);
  }

  char line[64];
  if (weather.alert.active) {
    snprintf(line, sizeof(line), "%s%s预警", weather.alert.eventName,
             weather::alertColorText(weather.alert.color));
    const int16_t width = static_cast<int16_t>(textWidth(kFontBodyBold, line) + 24);
    canvas.fillRect(right - width, 20, width, 32, kRed);
    drawTextAligned(canvas, kFontBodyBold, right - width / 2, 43, line, kWhite,
                    Align::Center);
  } else if (!model.configured) {
    drawTextAligned(canvas, kFontBody, right, 44, "未配置天气", kRed,
                    Align::Right);
  } else if (weather.fetchedEpoch > 0) {
    const LocalTime fetched = toLocal(weather.fetchedEpoch);
    char clock[8];
    formatClock(clock, sizeof(clock), fetched.minute);
    if (app::weatherStale(model)) {
      snprintf(line, sizeof(line), "数据过期·%02u-%02u %s",
               fetched.date.month, fetched.date.day, clock);
      drawTextAligned(canvas, kFontBodyBold, right, 44, line, kRed,
                      Align::Right);
    } else {
      snprintf(line, sizeof(line), "%s 更新", clock);
      drawTextAligned(canvas, kFontBody, right, 44, line, kBlack, Align::Right);
    }
  } else {
    drawTextAligned(canvas, kFontBody, right, 44, "等待天气数据", kBlack,
                    Align::Right);
  }
  hline(canvas, kLeft, kRight, 61, 2);
}

void Renderer::drawToday(Adafruit_GFX& canvas,
                         const app::UiModel& model) const {
  if (!model.clockSynced) {
    drawText(canvas, kFontHero, kLeft - 4, 158, "--", kBlack);
    drawText(canvas, kFontTitle, 136, 112, "等待校时", kBlack);
    drawText(canvas, kFontBody, 136, 142, "联网后自动同步日期", kBlack);
    hline(canvas, kLeft, kPanelRight, 184);
    return;
  }

  const LocalTime now = toLocal(model.nowEpoch);
  const cal::Date& date = now.date;
  char text[48];
  snprintf(text, sizeof(text), "%u", date.day);
  drawText(canvas, kFontHero, kLeft - 4, 158, text, kBlack);

  const uint8_t weekday = cal::weekdayMonday0(date);
  drawText(canvas, kFontTitle, 136, 106, cal::weekdayName(weekday), kBlack);

  char lunar[32];
  cal::formatLunar(date, lunar, sizeof(lunar));
  snprintf(text, sizeof(text), "%s%s%s", cal::monthName(date.month),
           lunar[0] != '\0' ? "·" : "", lunar);
  drawText(canvas, kFontBody, 136, 134, text, kBlack);

  int16_t x = 136;
  const char* festival = cal::festivalName(date);
  const int8_t term = cal::solarTermIndex(date);
  if (festival != nullptr) {
    x = static_cast<int16_t>(chip(canvas, x, 145, festival, kRed, kWhite) + 8);
  }
  if (term >= 0) {
    x = static_cast<int16_t>(
        chip(canvas, x, 145, cal::solarTermName(term), kRed, kWhite) + 8);
  }
  const cal::HolidayMark mark = cal::holidayMark(date);
  if (mark == cal::HolidayMark::Rest) {
    x = static_cast<int16_t>(chip(canvas, x, 145, "休", kRed, kWhite) + 8);
  } else if (mark == cal::HolidayMark::Work) {
    x = static_cast<int16_t>(chip(canvas, x, 145, "班", kBlack, kWhite) + 8);
  }
  snprintf(text, sizeof(text), "第 %u 周", cal::isoWeek(date));
  drawText(canvas, kFontSmall, x, 162, text, kBlack);
  hline(canvas, kLeft, kPanelRight, 184);
}

void Renderer::drawCurrentWeather(Adafruit_GFX& canvas,
                                  const app::UiModel& model) const {
  const app::WeatherSnapshot& weather = model.weather;
  const app::CurrentWeather& now = weather.now;
  char text[48];

  if (!model.configured || !now.valid) {
    drawWeatherIcon(canvas, weather::IconKind::Unknown, 74, 252, 96, kBlack,
                    kRed, kWhite);
    drawText(canvas, kFontTitle, 138, 240,
             model.configured ? "暂无天气" : "未配置天气", kBlack);
    drawText(canvas, kFontBody, 138, 270,
             model.configured ? "等待下次联网更新" : "请通过 USB 串口写入配置",
             kBlack);
  } else {
    drawWeatherIcon(canvas, weather::conditionIcon(now.code), 74, 252, 96,
                    kBlack, kRed, kWhite);
    formatTemperature(text, sizeof(text), now.temperatureC);
    const int16_t end = drawText(canvas, kFontHero, 138, 290, text, kBlack);
    drawText(canvas, kFontHeading, end + 2, 254, "°", kBlack);
    const int16_t column = end + 30 > 258 ? static_cast<int16_t>(end + 30) : 258;

    const bool severe = weather::conditionSevere(now.code);
    drawText(canvas, kFontTitle, column, 230, weather::conditionText(now.code),
             severe ? kRed : kBlack);
    const app::DailyForecast& today = weather.days[0];
    if (today.valid) {
      char high[8], low[8];
      formatTemperature(high, sizeof(high), today.maxC);
      formatTemperature(low, sizeof(low), today.minC);
      snprintf(text, sizeof(text), "%s° / %s°", high, low);
      drawText(canvas, kFontBody, column, 262, text, kBlack);
    }
    if (now.feelsLikeC != app::kNoValue) {
      snprintf(text, sizeof(text), "体感 %d°", now.feelsLikeC);
      drawText(canvas, kFontBody, column, 288, text, kBlack);
    }
  }
  hline(canvas, kLeft, kPanelRight, 306);

  // 3 x 2 facts grid.
  struct Cell {
    const char* label;
    char value[24];
    bool alert;
  } cells[6] = {};
  cells[0].label = "风";
  if (now.valid && now.windDegree >= 0) {
    if (now.windScale >= 0) {
      snprintf(cells[0].value, sizeof(cells[0].value), "%s %d级",
               weather::windDirection(now.windDegree), now.windScale);
    } else {
      snprintf(cells[0].value, sizeof(cells[0].value), "%s",
               weather::windDirection(now.windDegree));
    }
  } else {
    snprintf(cells[0].value, sizeof(cells[0].value), "--");
  }
  cells[1].label = "空气";
  if (weather.air.valid) {
    snprintf(cells[1].value, sizeof(cells[1].value), "%d %s", weather.air.aqi,
             weather::aqiShort(weather.air.aqi));
    cells[1].alert = weather::aqiPoor(weather.air.aqi);
  } else {
    snprintf(cells[1].value, sizeof(cells[1].value), "--");
  }
  cells[2].label = "湿度";
  if (now.valid && now.humidityPercent >= 0) {
    snprintf(cells[2].value, sizeof(cells[2].value), "%d%%",
             now.humidityPercent);
  } else {
    snprintf(cells[2].value, sizeof(cells[2].value), "--");
  }
  const app::DailyForecast& today = weather.days[0];
  cells[3].label = "日出";
  formatClock(cells[3].value, sizeof(cells[3].value),
              today.valid ? today.sunriseMinute : -1);
  cells[4].label = "日落";
  formatClock(cells[4].value, sizeof(cells[4].value),
              today.valid ? today.sunsetMinute : -1);
  cells[5].label = "降水";
  if (today.valid && today.precipitationPercent >= 0) {
    snprintf(cells[5].value, sizeof(cells[5].value), "%d%%",
             today.precipitationPercent);
  } else {
    snprintf(cells[5].value, sizeof(cells[5].value), "--");
  }

  for (int i = 0; i < 6; ++i) {
    const int16_t x = static_cast<int16_t>(kLeft + (i % 3) * 120);
    const int16_t y = static_cast<int16_t>(334 + (i / 3) * 30);
    const int16_t labelEnd = drawText(canvas, kFontBody, x, y, cells[i].label,
                                      kBlack);
    drawText(canvas, kFontBodyBold, labelEnd + 6, y, cells[i].value,
             cells[i].alert ? kRed : kBlack);
  }
}

void Renderer::drawCalendar(Adafruit_GFX& canvas,
                            const app::UiModel& model) const {
  if (!model.clockSynced) {
    drawText(canvas, kFontHeading, kCalendarLeft, 100, "日历", kBlack);
    drawText(canvas, kFontBody, kCalendarLeft, 140, "校时后显示当月日历", kBlack);
    return;
  }
  const LocalTime now = toLocal(model.nowEpoch);
  const cal::Date today = now.date;

  drawText(canvas, kFontHeading, kCalendarLeft, 100, cal::monthName(today.month),
           kBlack);
  char text[32];
  snprintf(text, sizeof(text), "%d  %s", today.year,
           kMonthEnglish[today.month - 1]);
  drawTextAligned(canvas, kFontBody, kRight, 99, text, kBlack, Align::Right);

  for (uint8_t i = 0; i < 7; ++i) {
    drawTextAligned(canvas, kFontBodyBold,
                    kCalendarLeft + i * kCalendarCell + kCalendarCell / 2, 130,
                    cal::weekdayShort(i), i >= 5 ? kRed : kBlack, Align::Center);
  }
  hline(canvas, kCalendarLeft, kRight, 140);

  cal::Date first = today;
  first.day = 1;
  const uint8_t firstColumn = cal::weekdayMonday0(first);
  const uint8_t count = cal::daysInMonth(today.year, today.month);
  const int16_t rowHeight = 38;

  for (uint8_t day = 1; day <= count; ++day) {
    const uint8_t index = static_cast<uint8_t>(firstColumn + day - 1);
    const int16_t column = index % 7;
    const int16_t row = index / 7;
    const int16_t cellLeft =
        static_cast<int16_t>(kCalendarLeft + column * kCalendarCell);
    const int16_t centerX = static_cast<int16_t>(cellLeft + kCalendarCell / 2);
    const int16_t top = static_cast<int16_t>(144 + row * rowHeight);

    cal::Date date = today;
    date.day = day;
    const cal::DayLabel label = cal::cellLabel(date);
    const cal::HolidayMark mark = cal::holidayMark(date);
    const bool isToday = day == today.day;

    uint16_t numberColor = kBlack;
    uint16_t labelColor = label.emphasis ? kRed : kBlack;
    if (isToday) {
      canvas.fillRect(cellLeft + 2, top, kCalendarCell - 4, rowHeight - 3, kRed);
      numberColor = kWhite;
      labelColor = kWhite;
    }
    // A 休/班 mark takes the top-right corner; nudge the number left for it.
    const int16_t numberX = static_cast<int16_t>(
        mark != cal::HolidayMark::None ? centerX - 6 : centerX);
    snprintf(text, sizeof(text), "%u", day);
    drawTextAligned(canvas, kFontNumber, numberX, top + 20, text, numberColor,
                    Align::Center);
    drawTextAligned(canvas, label.emphasis || isToday ? kFontSmallBold
                                                      : kFontSmall,
                    centerX, top + 33, label.text, labelColor, Align::Center);
    if (mark != cal::HolidayMark::None) {
      const uint16_t markColor =
          isToday ? kWhite : (mark == cal::HolidayMark::Rest ? kRed : kBlack);
      drawTextAligned(canvas, kFontSmallBold, cellLeft + kCalendarCell - 2,
                      top + 11,
                      mark == cal::HolidayMark::Rest ? "休" : "班", markColor,
                      Align::Right);
    }
  }
}

void Renderer::drawForecast(Adafruit_GFX& canvas,
                            const app::UiModel& model) const {
  hline(canvas, kLeft, kRight, 391, 2);
  const app::WeatherSnapshot& weather = model.weather;

  bool any = false;
  for (size_t i = 1; i < app::kForecastDays; ++i) any = any || weather.days[i].valid;
  if (!any) {
    drawTextAligned(canvas, kFontBody, 400, 440,
                    model.configured ? "暂无天气预报" : "配置和风天气后显示未来六天预报",
                    kBlack, Align::Center);
    return;
  }

  const float cellWidth = (kRight - kLeft) / 6.0f;
  char text[32];
  for (size_t i = 1; i < app::kForecastDays; ++i) {
    const app::DailyForecast& day = weather.days[i];
    const int16_t x = static_cast<int16_t>(kLeft + (i - 1) * cellWidth);
    if (i > 1) vline(canvas, x - 6, 404, 466);
    if (!day.valid) continue;

    const uint16_t code = day.codeDay;
    const bool severe = weather::conditionSevere(code) ||
                        weather::conditionSevere(day.codeNight);
    drawWeatherIcon(canvas, weather::conditionIcon(code), x + 22, 432, 42,
                    kBlack, kRed, kWhite);

    cal::Date date;
    date.year = day.year;
    date.month = day.month;
    date.day = day.day;
    const int16_t nameEnd = drawText(canvas, kFontBodyBold, x + 50, 416,
                                     kWeekShort[cal::weekdayMonday0(date)],
                                     kBlack);
    snprintf(text, sizeof(text), "%u", day.day);
    drawText(canvas, kFontSmall, nameEnd + 4, 416, text, kBlack);

    char high[8], low[8];
    formatTemperature(high, sizeof(high), day.maxC);
    formatTemperature(low, sizeof(low), day.minC);
    snprintf(text, sizeof(text), "%s–%s°", low, high);
    drawText(canvas, kFontBodyBold, x + 50, 441, text, kBlack);

    if (day.precipitationPercent >= 30) {
      snprintf(text, sizeof(text), "%s %d%%", weather::conditionText(code),
               day.precipitationPercent);
    } else {
      snprintf(text, sizeof(text), "%s", weather::conditionText(code));
    }
    drawText(canvas, severe ? kFontSmallBold : kFontSmall, x + 50, 464, text,
             severe ? kRed : kBlack);
  }
}

void Renderer::drawSystemPage(Adafruit_GFX& canvas,
                              const app::UiModel& model) const {
  drawHeader(canvas, model, "系统", "");
  char text[64];

  auto row = [&](int16_t x, int16_t y, const char* key, const char* value,
                 bool alert) {
    drawText(canvas, kFontBody, x, y, key, kBlack);
    drawTextAligned(canvas, kFontBodyBold, x + 352, y, value,
                    alert ? kRed : kBlack, Align::Right);
    canvas.drawFastHLine(x, y + 10, 352, kBlack);
  };

  // Left: power and wake schedule.
  const app::TelemetrySnapshot& t = model.telemetry;
  drawText(canvas, kFontBodyBold, kLeft, 104, "电源", kBlack);
  if (t.batteryPercent >= 0) {
    snprintf(text, sizeof(text), "~%d%%", t.batteryPercent);
  } else {
    snprintf(text, sizeof(text), "--");
  }
  row(kLeft, 138, "电量（估算）", text, lowBattery(t));
  snprintf(text, sizeof(text), "%u.%02u V", t.batteryMillivolts / 1000U,
           (t.batteryMillivolts % 1000U) / 10U);
  row(kLeft, 172, "电池电压", text, false);

  drawText(canvas, kFontBodyBold, kLeft, 222, "唤醒", kBlack);
  if (model.nextWakeMinutes >= 60) {
    snprintf(text, sizeof(text), "%lu 小时 %lu 分后",
             static_cast<unsigned long>(model.nextWakeMinutes / 60),
             static_cast<unsigned long>(model.nextWakeMinutes % 60));
  } else {
    snprintf(text, sizeof(text), "%lu 分钟后",
             static_cast<unsigned long>(model.nextWakeMinutes));
  }
  row(kLeft, 256, "下次唤醒", text, false);
  snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(model.wakeCount));
  row(kLeft, 290, "唤醒次数", text, false);
  const uint32_t up = model.uptimeSeconds;
  snprintf(text, sizeof(text), "%lu 分 %02lu 秒",
           static_cast<unsigned long>(up / 60), static_cast<unsigned long>(up % 60));
  row(kLeft, 324, "本次唤醒", text, false);
  row(kLeft, 358, "完整更新", "0:02 6:30 9 12 15 18 21", false);

  vline(canvas, 400, 84, 456);

  // Right: data and device.
  const int16_t x = 424;
  const app::WeatherSnapshot& w = model.weather;
  drawText(canvas, kFontBodyBold, x, 104, "天气数据", kBlack);
  row(x, 138, "最近一次", errorText(w.lastError),
      w.lastError != app::WeatherError::None);
  if (w.fetchedEpoch > 0) {
    const LocalTime fetched = toLocal(w.fetchedEpoch);
    char clock[8];
    formatClock(clock, sizeof(clock), fetched.minute);
    snprintf(text, sizeof(text), "%02u-%02u %s", fetched.date.month,
             fetched.date.day, clock);
  } else {
    snprintf(text, sizeof(text), "--");
  }
  row(x, 172, "成功更新", text, app::weatherStale(model));
  if (w.alert.active) {
    snprintf(text, sizeof(text), "%s%s 共 %u 条", w.alert.eventName,
             weather::alertColorText(w.alert.color), w.alert.count);
  } else {
    snprintf(text, sizeof(text), "无");
  }
  row(x, 206, "气象预警", text, w.alert.active);

  drawText(canvas, kFontBodyBold, x, 290, "设备", kBlack);
  if (model.network.connected) {
    snprintf(text, sizeof(text), "已连接·%d dBm", model.network.rssi);
  } else {
    snprintf(text, sizeof(text), "未连接");
  }
  row(x, 324, "Wi-Fi", text, !model.network.connected);
  row(x, 358, "固件", board::kFirmwareVersion, false);
  row(x, 392, "屏幕", "仅全刷·深度睡眠", false);
}

}  // namespace ui
