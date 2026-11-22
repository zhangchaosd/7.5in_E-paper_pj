#pragma once

#include <stdint.h>

#include "app/Models.h"

namespace weather {

enum class IconKind : uint8_t {
  Clear = 0,
  PartlyCloudy,
  Cloudy,
  Overcast,
  Rain,
  HeavyRain,
  Thunder,
  Snow,
  Sleet,
  Fog,
  Haze,
  Dust,
  Unknown,
};

// Chinese text for a QWeather condition code (weather-conditions table).
// The firmware owns these strings so the font character set stays closed;
// unknown future codes fall back to their hundred-group (雨/雪/雾...).
const char* conditionText(uint16_t code);
IconKind conditionIcon(uint16_t code);
// Heavy rain/snow, thunderstorms, sandstorms and severe haze.
bool conditionSevere(uint16_t code);

// 8-point Chinese compass direction for a meteorological degree (wind FROM).
const char* windDirection(int16_t degree);

// China HJ 633 AQI category: 优 / 良 / 轻度污染 / ...
const char* aqiCategory(int16_t aqi);
const char* aqiShort(int16_t aqi);  // 优 / 良 / 轻度 / 中度 / 重度 / 严重
bool aqiPoor(int16_t aqi);  // >= 151 (中度污染 and worse)

const char* alertColorText(app::AlertColor color);  // 蓝色 / 黄色 / ...

}  // namespace weather
