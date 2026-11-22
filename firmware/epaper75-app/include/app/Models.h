#pragma once
#ifndef EPAPER75_APP_MODELS_H_
#define EPAPER75_APP_MODELS_H_

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace app {

constexpr size_t kForecastDays = 7;  // [0] = today, [1..6] = strip
constexpr size_t kPlaceNameSize = 40;  // UTF-8
constexpr size_t kAlertNameSize = 40;  // UTF-8
constexpr int16_t kNoValue = INT16_MIN;

enum class DataAvailability : uint8_t {
  Unknown = 0,   // never fetched since power-on
  Available,     // last fetch succeeded
  Unavailable,   // last fetch failed; cached values may still be shown
};

enum class WeatherError : uint8_t {
  None = 0,
  NotConfigured,
  NoWifi,
  ClockNotSynced,
  Tls,
  Http,
  Unauthorized,
  RateLimited,
  Decompress,
  Parse,
  LowBattery,
};

const char* weatherErrorName(WeatherError error);

struct CurrentWeather {
  bool valid = false;
  int16_t temperatureC = kNoValue;
  int16_t feelsLikeC = kNoValue;
  uint16_t code = 999;           // QWeather condition code
  int8_t humidityPercent = -1;
  int16_t windDegree = -1;       // 0..359, -1 unknown
  int8_t windScale = -1;         // Beaufort-like scale
};

struct DailyForecast {
  bool valid = false;
  int16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  int16_t maxC = kNoValue;
  int16_t minC = kNoValue;
  uint16_t codeDay = 999;
  uint16_t codeNight = 999;
  int8_t precipitationPercent = -1;  // max of day/night probability
  int16_t sunriseMinute = -1;        // minutes after local midnight
  int16_t sunsetMinute = -1;
};

struct AirQuality {
  bool valid = false;
  int16_t aqi = -1;  // China (cn-mee) AQI
};

enum class AlertColor : uint8_t { Unknown = 0, Blue, Yellow, Orange, Red, White };

struct WeatherAlert {
  bool active = false;
  AlertColor color = AlertColor::Unknown;
  char eventName[kAlertNameSize] = {};  // e.g. 暴雨
  uint8_t count = 0;                    // active alerts at the location
  uint32_t key = 0;                     // hash of alert ids, change detector
};

struct WeatherSnapshot {
  DataAvailability availability = DataAvailability::Unknown;
  WeatherError lastError = WeatherError::None;
  CurrentWeather now;
  DailyForecast days[kForecastDays];
  AirQuality air;
  WeatherAlert alert;
  int64_t fetchedEpoch = 0;  // UTC seconds of the last successful full fetch
};

struct PlaceSnapshot {
  bool valid = false;
  char name[kPlaceNameSize] = {};  // e.g. 东城区
  char city[kPlaceNameSize] = {};  // e.g. 北京
};

struct TelemetrySnapshot {
  bool sensorOk = false;
  float temperatureC = NAN;
  float humidityPercent = NAN;
  uint16_t batteryMillivolts = 0;
  // Voltage-derived estimate only; -1 means unavailable. Any rendering must
  // keep the '~' marker: the board has no fuel gauge.
  int8_t batteryPercent = -1;
};

struct NetworkSnapshot {
  bool connected = false;
  int16_t rssi = -127;
};

enum class Page : uint8_t { Home = 0, System = 1 };

struct UiModel {
  WeatherSnapshot weather;
  PlaceSnapshot place;
  TelemetrySnapshot telemetry;
  NetworkSnapshot network;
  // Local (UTC+8) wall clock at render time; valid only when clockSynced.
  bool clockSynced = false;
  int64_t nowEpoch = 0;
  bool configured = false;  // QWeather credentials and location present
  Page page = Page::Home;
  // System page facts.
  uint32_t nextWakeMinutes = 0;  // minutes until the next scheduled wake
  uint32_t wakeCount = 0;
  uint32_t uptimeSeconds = 0;
};

template <size_t N>
inline void copyText(char (&destination)[N], const char* source) {
  if (source == nullptr) source = "";
  strncpy(destination, source, N - 1);
  destination[N - 1] = '\0';
}

// Data older than this is marked stale on screen.
constexpr int64_t kStaleAfterSeconds = 6 * 60 * 60;

inline bool weatherStale(const UiModel& model) {
  if (model.weather.fetchedEpoch <= 0 || !model.clockSynced) return true;
  return model.nowEpoch - model.weather.fetchedEpoch > kStaleAfterSeconds;
}

}  // namespace app

#endif  // EPAPER75_APP_MODELS_H_
