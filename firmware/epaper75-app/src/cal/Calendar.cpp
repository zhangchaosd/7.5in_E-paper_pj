#include "cal/Calendar.h"

#include <stdio.h>
#include <string.h>

#include "cal/CalendarData.h"

namespace cal {
namespace {

constexpr int32_t kEpochDays = 20089;  // daysFromCivil(2025, 1, 1)

const char* const kTermNames[24] = {
    "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
    "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
    "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至"};

const char* const kLunarMonthNames[12] = {"正月", "二月", "三月", "四月",
                                      "五月", "六月", "七月", "八月",
                                      "九月", "十月", "冬月", "腊月"};

const char* const kLunarDays[30] = {
    "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八",
    "初九", "初十", "十一", "十二", "十三", "十四", "十五", "十六",
    "十七", "十八", "十九", "二十", "廿一", "廿二", "廿三", "廿四",
    "廿五", "廿六", "廿七", "廿八", "廿九", "三十"};

const char* const kWeekdays[7] = {"星期一", "星期二", "星期三", "星期四",
                                  "星期五", "星期六", "星期日"};
const char* const kWeekdayShort[7] = {"一", "二", "三", "四",
                                      "五", "六", "日"};
const char* const kMonths[12] = {"一月", "二月", "三月", "四月",
                                 "五月", "六月", "七月", "八月",
                                 "九月", "十月", "十一月", "十二月"};

// Official State Council schedules (国办发明电). Each entry is one day.
// Update this table every November when the next year's notice is issued.
struct HolidayEntry {
  int16_t year;
  uint8_t month;
  uint8_t day;
  HolidayMark mark;
};

constexpr HolidayMark R = HolidayMark::Rest;
constexpr HolidayMark W = HolidayMark::Work;

constexpr HolidayEntry kHolidays[] = {
    // 2026 (published 2025-11-04)
    {2026, 1, 1, R}, {2026, 1, 2, R}, {2026, 1, 3, R}, {2026, 1, 4, W},
    {2026, 2, 14, W}, {2026, 2, 15, R}, {2026, 2, 16, R}, {2026, 2, 17, R},
    {2026, 2, 18, R}, {2026, 2, 19, R}, {2026, 2, 20, R}, {2026, 2, 21, R},
    {2026, 2, 22, R}, {2026, 2, 23, R}, {2026, 2, 28, W},
    {2026, 4, 4, R}, {2026, 4, 5, R}, {2026, 4, 6, R},
    {2026, 5, 1, R}, {2026, 5, 2, R}, {2026, 5, 3, R}, {2026, 5, 4, R},
    {2026, 5, 5, R}, {2026, 5, 9, W},
    {2026, 6, 19, R}, {2026, 6, 20, R}, {2026, 6, 21, R},
    {2026, 9, 20, W}, {2026, 9, 25, R}, {2026, 9, 26, R}, {2026, 9, 27, R},
    {2026, 10, 1, R}, {2026, 10, 2, R}, {2026, 10, 3, R}, {2026, 10, 4, R},
    {2026, 10, 5, R}, {2026, 10, 6, R}, {2026, 10, 7, R}, {2026, 10, 10, W},
    // 2027 (published 2026-11-05, 国办发明电〔2026〕10号)
    {2027, 1, 1, R}, {2027, 1, 2, R}, {2027, 1, 3, R}, {2027, 1, 31, W},
    {2027, 2, 5, R}, {2027, 2, 6, R}, {2027, 2, 7, R}, {2027, 2, 8, R},
    {2027, 2, 9, R}, {2027, 2, 10, R}, {2027, 2, 11, R}, {2027, 2, 12, R},
    {2027, 2, 13, R}, {2027, 2, 14, W},
    {2027, 4, 3, R}, {2027, 4, 4, R}, {2027, 4, 5, R},
    {2027, 5, 1, R}, {2027, 5, 2, R}, {2027, 5, 3, R}, {2027, 5, 4, R},
    {2027, 5, 5, R}, {2027, 5, 8, W},
    {2027, 6, 9, R}, {2027, 9, 15, R},
    {2027, 9, 26, W}, {2027, 10, 1, R}, {2027, 10, 2, R}, {2027, 10, 3, R},
    {2027, 10, 4, R}, {2027, 10, 5, R}, {2027, 10, 6, R}, {2027, 10, 7, R},
    {2027, 10, 9, W},
};

bool lunarMonthIndex(int32_t dayIndex, uint16_t& index) {
  if (kLunarMonthCount == 0 || dayIndex < kLunarMonths[0].dayIndex) {
    return false;
  }
  uint16_t lo = 0;
  uint16_t hi = kLunarMonthCount - 1;
  while (lo < hi) {
    const uint16_t mid = static_cast<uint16_t>((lo + hi + 1) / 2);
    if (kLunarMonths[mid].dayIndex <= dayIndex) {
      lo = mid;
    } else {
      hi = static_cast<uint16_t>(mid - 1);
    }
  }
  // The final entry has no known length; refuse dates past the table.
  if (lo == kLunarMonthCount - 1) return false;
  index = lo;
  return true;
}

}  // namespace

int32_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

Date civilFromDays(int32_t z) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t y = static_cast<int32_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  Date out;
  out.year = static_cast<int16_t>(y + (m <= 2 ? 1 : 0));
  out.month = static_cast<uint8_t>(m);
  out.day = static_cast<uint8_t>(d);
  return out;
}

uint8_t daysInMonth(int year, uint8_t month) {
  static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  }
  return kDays[month - 1];
}

uint8_t weekdayMonday0(const Date& date) {
  // 1970-01-01 was a Thursday (Monday-zero index 3).
  const int32_t days = daysFromCivil(date.year, date.month, date.day);
  const int32_t w = (days % 7 + 7 + 3) % 7;
  return static_cast<uint8_t>(w);
}

uint8_t isoWeek(const Date& date) {
  const int32_t days = daysFromCivil(date.year, date.month, date.day);
  const int32_t thursday = days - weekdayMonday0(date) + 3;
  const Date thursdayDate = civilFromDays(thursday);
  const int32_t jan1 = daysFromCivil(thursdayDate.year, 1, 1);
  return static_cast<uint8_t>((thursday - jan1) / 7 + 1);
}

bool inDataRange(const Date& date) {
  return date.year >= kDataFirstYear && date.year <= kDataLastYear;
}

LunarDate lunarDate(const Date& date) {
  LunarDate out;
  const int32_t index =
      daysFromCivil(date.year, date.month, date.day) - kEpochDays;
  uint16_t month = 0;
  if (!lunarMonthIndex(index, month)) return out;
  out.month = kLunarMonths[month].month;
  out.leap = kLunarMonths[month].leap != 0;
  out.day = static_cast<uint8_t>(index - kLunarMonths[month].dayIndex + 1);
  return out;
}

int8_t solarTermIndex(const Date& date) {
  if (!inDataRange(date) || date.month < 1 || date.month > 12) return -1;
  const uint8_t* row = kSolarTermDay[date.year - kDataFirstYear];
  const uint8_t first = static_cast<uint8_t>((date.month - 1) * 2);
  if (row[first] == date.day) return static_cast<int8_t>(first);
  if (row[first + 1] == date.day) return static_cast<int8_t>(first + 1);
  return -1;
}

const char* solarTermName(int8_t index) {
  return index >= 0 && index < 24 ? kTermNames[index] : "";
}

const char* festivalName(const Date& date) {
  if (date.month == 1 && date.day == 1) return "元旦";
  if (date.month == 5 && date.day == 1) return "劳动节";
  if (date.month == 6 && date.day == 1) return "儿童节";
  if (date.month == 10 && date.day == 1) return "国庆节";

  const LunarDate lunar = lunarDate(date);
  if (lunar.month == 0 || lunar.leap) return nullptr;
  if (lunar.month == 1 && lunar.day == 1) return "春节";
  if (lunar.month == 1 && lunar.day == 15) return "元宵";
  if (lunar.month == 5 && lunar.day == 5) return "端午";
  if (lunar.month == 7 && lunar.day == 7) return "七夕";
  if (lunar.month == 8 && lunar.day == 15) return "中秋";
  if (lunar.month == 9 && lunar.day == 9) return "重阳";
  if (lunar.month == 12 && lunar.day == 8) return "腊八";
  if (lunar.month == 12) {
    // 除夕 is the last day of the twelfth month (29th or 30th).
    const int32_t days = daysFromCivil(date.year, date.month, date.day);
    const Date tomorrow = civilFromDays(days + 1);
    const LunarDate next = lunarDate(tomorrow);
    if (next.month == 1 && next.day == 1 && !next.leap) return "除夕";
  }
  return nullptr;
}

HolidayMark holidayMark(const Date& date) {
  for (const HolidayEntry& entry : kHolidays) {
    if (entry.year == date.year && entry.month == date.month &&
        entry.day == date.day) {
      return entry.mark;
    }
  }
  return HolidayMark::None;
}

const char* lunarMonthName(uint8_t month) {
  return month >= 1 && month <= 12 ? kLunarMonthNames[month - 1] : "";
}

const char* lunarDayName(uint8_t day) {
  return day >= 1 && day <= 30 ? kLunarDays[day - 1] : "";
}

const char* weekdayName(uint8_t mondayZero) {
  return mondayZero < 7 ? kWeekdays[mondayZero] : "";
}

const char* weekdayShort(uint8_t mondayZero) {
  return mondayZero < 7 ? kWeekdayShort[mondayZero] : "";
}

const char* monthName(uint8_t month) {
  return month >= 1 && month <= 12 ? kMonths[month - 1] : "";
}

DayLabel cellLabel(const Date& date) {
  DayLabel label;
  if (const char* festival = festivalName(date)) {
    strlcpy(label.text, festival, sizeof(label.text));
    label.emphasis = true;
    return label;
  }
  const int8_t term = solarTermIndex(date);
  if (term >= 0) {
    strlcpy(label.text, solarTermName(term), sizeof(label.text));
    label.emphasis = true;
    return label;
  }
  const LunarDate lunar = lunarDate(date);
  if (lunar.month == 0) return label;
  if (lunar.day == 1) {
    snprintf(label.text, sizeof(label.text), "%s%s", lunar.leap ? "闰" : "",
             lunarMonthName(lunar.month));
  } else {
    strlcpy(label.text, lunarDayName(lunar.day), sizeof(label.text));
  }
  return label;
}

void formatLunar(const Date& date, char* output, size_t capacity) {
  if (capacity == 0) return;
  const LunarDate lunar = lunarDate(date);
  if (lunar.month == 0) {
    output[0] = '\0';
    return;
  }
  snprintf(output, capacity, "农历%s%s%s", lunar.leap ? "闰" : "",
           lunarMonthName(lunar.month), lunarDayName(lunar.day));
}

}  // namespace cal
