// Host unit tests for pure logic: parsers, schedule, settings parsing,
// base64 and display signatures.  Run: tools/hosttest/run_host_tests.sh
#include <cstdio>
#include <cstring>
#include "app/DisplaySignature.h"
#include "app/Schedule.h"
#include "cal/Calendar.h"
#include "net/Base64.h"
#include "net/SettingsStore.h"
#include "net/WeatherClient.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int64_t beijing(int y, int mo, int d, int h, int mi) {
  return int64_t(cal::daysFromCivil(y, mo, d)) * 86400 + h * 3600 + mi * 60 - 8 * 3600;
}

int main() {
  // ISO 8601
  int64_t e = 0;
  CHECK(net::parseIso8601("2026-11-22T06:31+08:00", e) && e == beijing(2026, 11, 22, 6, 31));
  CHECK(net::parseIso8601("2026-11-21T22:31Z", e) && e == beijing(2026, 11, 22, 6, 31));
  CHECK(net::parseIso8601("2026-11-22T06:31:20.000+0800", e) && e == beijing(2026, 11, 22, 6, 31) + 20);
  CHECK(!net::parseIso8601("garbage", e));

  // Current conditions (v1 shape).
  const char* current = R"({"validTime":"2026-11-22T18:30+08:00","condition":{"code":"101","text":"多云"},
    "temperature":{"value":14.4,"unit":"C"},"feelsLike":{"value":12.2},"humidity":0.68,
    "wind":{"direction":{"degree":44,"compass":"NE"},"speed":{"value":11},"scale":3}})";
  app::CurrentWeather now;
  CHECK(net::parseCurrent(current, strlen(current), now));
  CHECK(now.valid && now.temperatureC == 14 && now.feelsLikeC == 12 && now.code == 101);
  CHECK(now.humidityPercent == 68 && now.windDegree == 44 && now.windScale == 3);

  // Daily forecast with UTC period boundaries.
  const char* daily = R"({"days":[
    {"forecastStartTime":"2026-11-21T16:00Z","forecastEndTime":"2026-11-22T16:00Z",
     "temperatureMax":{"value":18},"temperatureMin":{"value":11},
     "daytime":{"condition":{"code":101},"precipitation":{"probability":0.1}},
     "nighttime":{"condition":{"code":"104"},"precipitation":{"probability":0.2}},
     "astro":{"sunrise":"2026-11-22T06:31+08:00","sunset":"2026-11-22T16:53+08:00"}},
    {"forecastStartTime":"2026-11-23T00:00+08:00","forecastEndTime":"2026-11-24T00:00+08:00",
     "temperatureMax":{"value":15.4},"temperatureMin":{"value":10.6},
     "daytime":{"condition":{"code":"305"},"precipitation":{"probability":0.6}},
     "nighttime":{"condition":{"code":"305"},"precipitation":{"probability":0.3}}}]})";
  app::DailyForecast days[app::kForecastDays];
  CHECK(net::parseDaily(daily, strlen(daily), days));
  CHECK(days[0].valid && days[0].month == 11 && days[0].day == 22 && days[0].maxC == 18 && days[0].minC == 11);
  CHECK(days[0].codeDay == 101 && days[0].codeNight == 104 && days[0].precipitationPercent == 20);
  CHECK(days[0].sunriseMinute == 6 * 60 + 31 && days[0].sunsetMinute == 16 * 60 + 53);
  CHECK(days[1].valid && days[1].day == 23 && days[1].maxC == 15 && days[1].minC == 11 && days[1].precipitationPercent == 60);
  CHECK(!days[2].valid);

  // Air quality picks cn-mee.
  const char* air = R"({"indexes":[{"code":"us-epa","aqi":80},{"code":"cn-mee","aqi":52,"aqiDisplay":"52","category":"良"}]})";
  app::AirQuality aq;
  CHECK(net::parseAir(air, strlen(air), aq) && aq.aqi == 52);

  // Alerts: two active, one cancelled; worst colour wins.
  const char* alerts = R"({"metadata":{"zeroResult":false},"alerts":[
    {"id":"a1","eventType":{"name":"大风","code":"1006"},"color":{"code":"blue"},"messageType":{"code":"alert"}},
    {"id":"a2","eventType":{"name":"暴雨","code":"1003"},"color":{"code":"yellow"},"messageType":{"code":"update"}},
    {"id":"a3","eventType":{"name":"大雾"},"color":{"code":"orange"},"messageType":{"code":"cancel"}}]})";
  app::WeatherAlert alert;
  CHECK(net::parseAlerts(alerts, strlen(alerts), alert));
  CHECK(alert.active && alert.count == 2 && alert.color == app::AlertColor::Yellow && strcmp(alert.eventName, "暴雨") == 0 && alert.key != 0);
  const char* none = R"({"metadata":{"zeroResult":true},"alerts":[]})";
  app::WeatherAlert quiet;
  CHECK(net::parseAlerts(none, strlen(none), quiet) && !quiet.active && quiet.key == 0);

  // GeoAPI.
  const char* geo = R"({"code":"200","location":[{"name":"东城","id":"101011600","adm2":"北京","adm1":"北京市"}]})";
  app::PlaceSnapshot place;
  CHECK(net::parsePlace(geo, strlen(geo), place) && strcmp(place.name, "东城") == 0 && strcmp(place.city, "北京") == 0);

  // Coordinates.
  int16_t c = 0;
  CHECK(net::parseCoordinate("39.92", 9000, c) && c == 3992);
  CHECK(net::parseCoordinate("116.4149", 18000, c) && c == 11641);
  CHECK(net::parseCoordinate("-0.005", 9000, c) && c == -1);
  CHECK(!net::parseCoordinate("91", 9000, c));
  CHECK(!net::parseCoordinate("31.2x", 9000, c));

  // Ed25519 PKCS#8 body and raw seed.
  uint8_t seed[32] = {};
  CHECK(net::parseEd25519PrivateKey("MC4CAQAwBQYDK2VwBCIEIAABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4f", seed) && seed[0] == 0 && seed[31] == 31);
  CHECK(!net::parseEd25519PrivateKey("MCowBQYDK2VwAyEAAAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=", seed));
  CHECK(net::validateApiHost("abcxyz.qweatherapi.com") && !net::validateApiHost("https://x.com") && !net::validateApiHost("nodot"));
  CHECK(net::validateQWeatherId("Q12345ABCD") && !net::validateQWeatherId("bad id"));

  // base64url (RFC 4648 vectors, unpadded).
  char b[16];
  net::base64UrlEncode(reinterpret_cast<const uint8_t*>("foob"), 4, b, sizeof(b));
  CHECK(strcmp(b, "Zm9vYg") == 0);
  const uint8_t bytes[] = {0xfb, 0xff};
  net::base64UrlEncode(bytes, 2, b, sizeof(b));
  CHECK(strcmp(b, "-_8") == 0);

  // Schedule.
  app::WakePlan p = app::nextScheduledWake(beijing(2026, 11, 22, 18, 40));
  CHECK(p.epoch == beijing(2026, 11, 22, 19, 0) && p.kind == app::WakeKind::AlertCheck);
  p = app::nextScheduledWake(beijing(2026, 11, 22, 22, 0));
  CHECK(p.epoch == beijing(2026, 11, 23, 0, 2) && p.kind == app::WakeKind::FullUpdate);
  p = app::nextScheduledWake(beijing(2026, 11, 23, 0, 2));
  CHECK(p.epoch == beijing(2026, 11, 23, 6, 30) && p.kind == app::WakeKind::FullUpdate);
  p = app::nextScheduledWake(beijing(2026, 11, 23, 8, 59));
  CHECK(p.epoch == beijing(2026, 11, 23, 9, 0) && p.kind == app::WakeKind::FullUpdate);
  int full = 0, alertsPerDay = 0;
  int64_t t = beijing(2026, 11, 23, 0, 3);
  while (t < beijing(2026, 11, 24, 0, 3)) {
    p = app::nextScheduledWake(t);
    if (p.epoch >= beijing(2026, 11, 24, 0, 3)) break;
    (p.kind == app::WakeKind::FullUpdate ? full : alertsPerDay)++;
    t = p.epoch;
  }
  CHECK(full == 7 && alertsPerDay == 11);  // 6 daytime + 00:02 midnight
  printf("schedule: %d full updates (incl. midnight), %d alert checks per day\n", full, alertsPerDay);

  // Signatures ignore fetch minute and uptime, notice visible changes.
  app::UiModel m;
  m.configured = true; m.clockSynced = true; m.nowEpoch = beijing(2026, 11, 22, 18, 40);
  m.weather.fetchedEpoch = beijing(2026, 11, 22, 18, 30);
  m.weather.now.valid = true; m.weather.now.temperatureC = 14;
  const uint32_t s0 = app::visibleSignature(m);
  app::UiModel m2 = m; m2.weather.fetchedEpoch += 3 * 3600; m2.nowEpoch += 3 * 3600; m2.uptimeSeconds = 99;
  CHECK(app::visibleSignature(m2) == s0);
  m2.weather.now.temperatureC = 15;
  CHECK(app::visibleSignature(m2) != s0);
  app::UiModel m3 = m; m3.nowEpoch = beijing(2026, 11, 23, 0, 2); m3.weather.fetchedEpoch = m3.nowEpoch;
  CHECK(app::visibleSignature(m3) != s0);  // new calendar day
  app::UiModel m4 = m; m4.nowEpoch += 7 * 3600;  // data becomes stale
  CHECK(app::visibleSignature(m4) != s0);

  printf(failures ? "%d FAILURES\n" : "all host tests passed\n", failures);
  return failures ? 1 : 0;
}
