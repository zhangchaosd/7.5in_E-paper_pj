#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cal {

// Civil (proleptic Gregorian) date in Beijing time.
struct Date {
  int16_t year = 0;
  uint8_t month = 0;  // 1..12
  uint8_t day = 0;    // 1..31
};

enum class HolidayMark : uint8_t { None = 0, Rest, Work };

struct LunarDate {
  uint8_t month = 0;  // 1..12, 0 = outside the generated table
  uint8_t day = 0;    // 1..30
  bool leap = false;
};

// Text and emphasis for one calendar cell's second line.
struct DayLabel {
  char text[16] = {};  // UTF-8, at most three CJK characters
  bool emphasis = false;  // festival or solar term
};

int32_t daysFromCivil(int year, unsigned month, unsigned day);
Date civilFromDays(int32_t days);
uint8_t daysInMonth(int year, uint8_t month);
// 0 = Monday ... 6 = Sunday.
uint8_t weekdayMonday0(const Date& date);
// ISO-8601 week number.
uint8_t isoWeek(const Date& date);

bool inDataRange(const Date& date);
LunarDate lunarDate(const Date& date);
// -1 if no solar term starts on this date, else 0 (小寒) .. 23 (冬至).
int8_t solarTermIndex(const Date& date);
const char* solarTermName(int8_t index);
// Traditional or public festival name, or nullptr.
const char* festivalName(const Date& date);
HolidayMark holidayMark(const Date& date);

const char* lunarMonthName(uint8_t month);  // 正月 .. 腊月
const char* lunarDayName(uint8_t day);      // 初一 .. 三十
const char* weekdayName(uint8_t mondayZero);  // 星期一 .. 星期日
const char* weekdayShort(uint8_t mondayZero);  // 一 .. 日
const char* monthName(uint8_t month);        // 一月 .. 十二月

// Festival > solar term > month name on 初一 > lunar day.
DayLabel cellLabel(const Date& date);
// e.g. "农历十月十四" / "农历闰六月初二"; empty when outside the table.
void formatLunar(const Date& date, char* output, size_t capacity);

}  // namespace cal
