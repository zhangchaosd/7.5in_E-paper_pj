#pragma once

#include <Arduino.h>

#include "app/Models.h"
#include "net/SettingsStore.h"

namespace net {

// Synchronous QWeather client. Every call opens one verified TLS connection
// to the account-specific API Host, authenticates with a short-lived Ed25519
// JWT and reads a bounded, usually gzip-compressed JSON body. Requires Wi-Fi
// and a synchronized clock (the JWT carries iat/exp).
class WeatherClient {
 public:
  explicit WeatherClient(const WeatherSettings& settings);
  ~WeatherClient();

  // Current conditions, 7-day forecast, air quality and alerts. On success
  // replaces `output` entirely; on failure leaves it untouched.
  app::WeatherError fetchAll(int64_t nowEpoch, app::WeatherSnapshot& output);
  // Alerts only, for cheap hourly checks.
  app::WeatherError fetchAlerts(int64_t nowEpoch, app::WeatherAlert& output);
  // Resolves the configured coordinates to a district/city name.
  app::WeatherError lookupPlace(int64_t nowEpoch, app::PlaceSnapshot& output);

  int lastHttpCode() const { return lastHttpCode_; }
  // mbedTLS/transport error text for the last failed request (not secret).
  const char* lastTransportError() const { return lastTransportError_; }
  uint32_t heapBeforeConnect() const { return heapBeforeConnect_; }
  size_t lastBodyLength() const { return lastBodyLength_; }

 private:
  // Performs GET path and leaves the decoded JSON in body_/bodyLength_.
  app::WeatherError get(const char* path, int64_t nowEpoch);
  bool makeJwt(int64_t nowEpoch, char* output, size_t capacity) const;
  void coordinates(char* output, size_t capacity, bool lonFirst) const;

  WeatherSettings settings_;
  uint8_t* raw_ = nullptr;
  uint8_t* body_ = nullptr;
  size_t bodyLength_ = 0;
  int lastHttpCode_ = 0;
  char lastTransportError_[96] = {};
  uint32_t heapBeforeConnect_ = 0;
  size_t lastBodyLength_ = 0;
};

// Exposed for host-side tests of the parsers.
bool parseCurrent(const char* json, size_t length, app::CurrentWeather& out);
bool parseDaily(const char* json, size_t length,
                app::DailyForecast (&out)[app::kForecastDays]);
bool parseAir(const char* json, size_t length, app::AirQuality& out);
bool parseAlerts(const char* json, size_t length, app::WeatherAlert& out);
bool parsePlace(const char* json, size_t length, app::PlaceSnapshot& out);
// ISO-8601 "2026-11-22T06:31+08:00" / "...Z" / with seconds -> UTC epoch.
bool parseIso8601(const char* text, int64_t& epoch);

}  // namespace net
