#include "app/DisplaySignature.h"

#include <math.h>

namespace app {
namespace {

class Hasher {
 public:
  void add(const void* data, size_t length) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
      hash_ ^= p[i];
      hash_ *= 16777619U;
    }
  }
  template <typename T>
  void value(T v) {
    add(&v, sizeof(v));
  }
  void text(const char* s) {
    while (s != nullptr && *s) value(*s++);
    value('\0');
  }
  uint32_t result() const { return hash_; }

 private:
  uint32_t hash_ = 2166136261U;
};

int32_t localDay(int64_t epoch) {
  const int64_t local = epoch + 8 * 3600;
  int64_t day = local / 86400;
  if (local % 86400 < 0) --day;
  return static_cast<int32_t>(day);
}

}  // namespace

uint32_t visibleSignature(const UiModel& m) {
  Hasher h;
  h.value(static_cast<uint8_t>(m.page));
  h.value(m.clockSynced);
  h.value(m.clockSynced ? localDay(m.nowEpoch) : -1);
  h.value(m.configured);
  h.value(m.place.valid);
  h.text(m.place.name);
  h.text(m.place.city);

  const WeatherSnapshot& w = m.weather;
  h.value(w.availability == DataAvailability::Unavailable);
  h.value(weatherStale(m));
  h.value(w.fetchedEpoch > 0);
  h.value(w.alert.active);
  if (w.alert.active) {
    h.value(static_cast<uint8_t>(w.alert.color));
    h.text(w.alert.eventName);
  }
  const TelemetrySnapshot& t = m.telemetry;
  h.value(t.batteryPercent >= 0 && t.batteryPercent <= 15);

  if (m.page == Page::Home) {
    h.value(w.now.valid);
    if (w.now.valid) {
      h.value(w.now.temperatureC);
      h.value(w.now.feelsLikeC);
      h.value(w.now.code);
      h.value(w.now.humidityPercent);
      h.value(static_cast<int16_t>(w.now.windDegree < 0 ? -1 : ((w.now.windDegree * 2 + 45) / 90) % 8));
      h.value(w.now.windScale);
    }
    for (const DailyForecast& d : w.days) {
      h.value(d.valid);
      if (!d.valid) continue;
      h.value(d.year);
      h.value(d.month);
      h.value(d.day);
      h.value(d.maxC);
      h.value(d.minC);
      h.value(d.codeDay);
      h.value(d.codeNight);
      h.value(static_cast<int8_t>(d.precipitationPercent >= 30 ? d.precipitationPercent : 0));
    }
    h.value(w.days[0].sunriseMinute);
    h.value(w.days[0].sunsetMinute);
    h.value(w.air.valid);
    h.value(w.air.aqi);
    // Today's precipitation probability is shown exactly in the facts grid.
    h.value(w.days[0].precipitationPercent);
  } else {
    // System page: fetch result, battery and sensor at displayed precision.
    h.value(static_cast<uint8_t>(w.lastError));
    h.value(w.fetchedEpoch);
    h.value(t.batteryPercent);
    h.value(static_cast<uint16_t>(t.batteryMillivolts / 10));
    h.value(m.network.connected);
  }
  return h.result();
}

}  // namespace app
