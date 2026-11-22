#include "net/WeatherClient.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <sodium.h>

#include "net/Base64.h"
#include "net/Gzip.h"

// Mozilla root CA bundle compiled into the ESP32 core's mbedTLS component.
extern const uint8_t kCrtBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t kCrtBundleEnd[] asm("_binary_x509_crt_bundle_end");
#endif

namespace net {
namespace {

constexpr int32_t kUtcOffsetSeconds = 8 * 60 * 60;
#ifdef ESP_PLATFORM
constexpr size_t kRawLimit = 32 * 1024;   // compressed body
constexpr size_t kBodyLimit = 96 * 1024;  // decompressed JSON
constexpr uint16_t kConnectTimeoutMs = 8000;
constexpr uint16_t kReadTimeoutMs = 10000;
#endif

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2 ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + doe - 719468;
}

void civilFromEpochLocal(int64_t epoch, int16_t& y, uint8_t& m, uint8_t& d,
                         int16_t& minute) {
  int64_t local = epoch + kUtcOffsetSeconds;
  int64_t days = local / 86400;
  int64_t rest = local % 86400;
  if (rest < 0) {
    rest += 86400;
    --days;
  }
  minute = static_cast<int16_t>(rest / 60);
  int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<uint8_t>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<uint8_t>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int16_t>(static_cast<int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0));
}

// Reads a number that may be a bare value, a numeric string, or an object
// with a "value" member (the v1 APIs wrap measurements as {value, unit}).
bool readNumber(JsonVariantConst v, float& out) {
  if (v.is<JsonObjectConst>()) v = v["value"];
  if (v.is<float>() || v.is<int>() || v.is<long>() || v.is<double>()) {
    out = v.as<float>();
    return isfinite(out);
  }
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    char* end = nullptr;
    const float parsed = strtof(s, &end);
    if (end != s && isfinite(parsed)) {
      out = parsed;
      return true;
    }
  }
  return false;
}

int16_t roundedOr(JsonVariantConst v, int16_t fallback) {
  float value = 0;
  if (!readNumber(v, value)) return fallback;
  return static_cast<int16_t>(lroundf(value));
}

// Probabilities and humidity arrive as 0..1 fractions in the v1 APIs; accept
// legacy 0..100 percentages as well.
int8_t percentOr(JsonVariantConst v, int8_t fallback) {
  float value = 0;
  if (!readNumber(v, value) || value < 0) return fallback;
  if (value <= 1.0f) value *= 100.0f;
  if (value > 100.0f) value = 100.0f;
  return static_cast<int8_t>(lroundf(value));
}

uint16_t conditionCode(JsonVariantConst condition) {
  JsonVariantConst code = condition["code"];
  if (code.isNull()) code = condition["icon"];
  float value = 0;
  if (!readNumber(code, value) || value < 100 || value > 999) return 999;
  return static_cast<uint16_t>(value);
}

int16_t localMinuteOf(JsonVariantConst v) {
  int64_t epoch = 0;
  if (!v.is<const char*>() || !parseIso8601(v.as<const char*>(), epoch)) return -1;
  int16_t y;
  uint8_t m, d;
  int16_t minute;
  civilFromEpochLocal(epoch, y, m, d, minute);
  return minute;
}

uint32_t fnv1a(uint32_t hash, const char* text) {
  if (text == nullptr) return hash;
  while (*text) {
    hash ^= static_cast<uint8_t>(*text++);
    hash *= 16777619U;
  }
  return hash;
}

app::AlertColor parseColor(JsonVariantConst v) {
  const char* text = nullptr;
  if (v.is<const char*>()) {
    text = v.as<const char*>();
  } else if (v.is<JsonObjectConst>()) {
    text = v["code"].as<const char*>();
    if (text == nullptr) text = v["name"].as<const char*>();
  }
  if (text == nullptr) return app::AlertColor::Unknown;
  char lower[16] = {};
  for (size_t i = 0; i < sizeof(lower) - 1 && text[i]; ++i) {
    lower[i] = static_cast<char>(tolower(static_cast<unsigned char>(text[i])));
  }
  if (strstr(lower, "blue") || strstr(text, "蓝")) return app::AlertColor::Blue;
  if (strstr(lower, "yellow") || strstr(text, "黄")) return app::AlertColor::Yellow;
  if (strstr(lower, "orange") || strstr(text, "橙")) return app::AlertColor::Orange;
  if (strstr(lower, "red") || strstr(text, "红")) return app::AlertColor::Red;
  if (strstr(lower, "white") || strstr(text, "白")) return app::AlertColor::White;
  return app::AlertColor::Unknown;
}

JsonArrayConst findAlertArray(JsonObjectConst root) {
  JsonArrayConst alerts = root["alerts"];
  if (!alerts.isNull()) return alerts;
  for (JsonPairConst pair : root) {
    JsonArrayConst candidate = pair.value().as<JsonArrayConst>();
    if (!candidate.isNull() && candidate.size() > 0 &&
        !candidate[0]["eventType"].isNull()) {
      return candidate;
    }
  }
  return JsonArrayConst();
}

}  // namespace

bool parseIso8601(const char* s, int64_t& epoch) {
  if (s == nullptr) return false;
  int y, mo, d, h, mi;
  int consumed = 0;
  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d%n", &y, &mo, &d, &h, &mi, &consumed) != 5) {
    return false;
  }
  const char* p = s + consumed;
  int sec = 0;
  if (*p == ':') {
    sec = atoi(p + 1);
    p += 3;
    if (*p == '.') {
      ++p;
      while (isdigit(static_cast<unsigned char>(*p))) ++p;
    }
  }
  int offsetSeconds = 0;
  if (*p == 'Z' || *p == 'z') {
    offsetSeconds = 0;
  } else if (*p == '+' || *p == '-') {
    const int sign = *p == '-' ? -1 : 1;
    int oh = 0, om = 0;
    if (sscanf(p + 1, "%2d:%2d", &oh, &om) < 1 && sscanf(p + 1, "%2d%2d", &oh, &om) < 1) {
      return false;
    }
    offsetSeconds = sign * (oh * 3600 + om * 60);
  } else if (*p == '\0') {
    offsetSeconds = kUtcOffsetSeconds;  // bare local time
  } else {
    return false;
  }
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59) return false;
  epoch = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 +
          h * 3600 + mi * 60 + sec - offsetSeconds;
  return true;
}

bool parseCurrent(const char* json, size_t length, app::CurrentWeather& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  // Accept both the v1 flat object and a {"now": {...}} wrapper.
  JsonObjectConst now = root["now"].is<JsonObjectConst>()
                            ? root["now"].as<JsonObjectConst>()
                            : root;
  app::CurrentWeather parsed;
  parsed.temperatureC = roundedOr(now["temperature"], app::kNoValue);
  if (parsed.temperatureC == app::kNoValue) {
    parsed.temperatureC = roundedOr(now["temp"], app::kNoValue);
  }
  if (parsed.temperatureC == app::kNoValue) return false;
  parsed.feelsLikeC = roundedOr(now["feelsLike"], app::kNoValue);
  parsed.code = conditionCode(now["condition"]);
  if (parsed.code == 999 && !now["icon"].isNull()) parsed.code = conditionCode(now);
  parsed.humidityPercent = percentOr(now["humidity"], -1);
  JsonVariantConst wind = now["wind"];
  parsed.windDegree = roundedOr(wind["direction"]["degree"], -1);
  if (parsed.windDegree < 0) parsed.windDegree = roundedOr(now["wind360"], -1);
  if (parsed.windDegree >= 360) parsed.windDegree %= 360;
  parsed.windScale = static_cast<int8_t>(roundedOr(wind["scale"], -1));
  if (parsed.windScale < 0) parsed.windScale = static_cast<int8_t>(roundedOr(now["windScale"], -1));
  parsed.valid = true;
  out = parsed;
  return true;
}

bool parseDaily(const char* json, size_t length,
                app::DailyForecast (&out)[app::kForecastDays]) {
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  JsonArrayConst days = doc["days"];
  if (days.isNull()) days = doc["daily"];
  if (days.isNull() || days.size() == 0) return false;

  app::DailyForecast parsed[app::kForecastDays];
  size_t index = 0;
  for (JsonObjectConst day : days) {
    if (index >= app::kForecastDays) break;
    app::DailyForecast& f = parsed[index];
    int64_t start = 0, end = 0;
    const bool haveStart = parseIso8601(day["forecastStartTime"] | "", start);
    const bool haveEnd = parseIso8601(day["forecastEndTime"] | "", end);
    if (haveStart || haveEnd) {
      // Midpoint is robust to UTC vs local period boundaries.
      const int64_t mid = haveStart && haveEnd ? (start + end) / 2
                                              : (haveStart ? start + 6 * 3600 : end - 6 * 3600);
      int16_t minute;
      civilFromEpochLocal(mid, f.year, f.month, f.day, minute);
    } else if (const char* date = day["fxDate"]) {
      int y = 0, m = 0, d = 0;
      if (sscanf(date, "%4d-%2d-%2d", &y, &m, &d) != 3) continue;
      f.year = static_cast<int16_t>(y);
      f.month = static_cast<uint8_t>(m);
      f.day = static_cast<uint8_t>(d);
    } else {
      continue;
    }
    f.maxC = roundedOr(day["temperatureMax"], roundedOr(day["tempMax"], app::kNoValue));
    f.minC = roundedOr(day["temperatureMin"], roundedOr(day["tempMin"], app::kNoValue));
    JsonVariantConst daytime = day["daytime"];
    JsonVariantConst nighttime = day["nighttime"];
    f.codeDay = conditionCode(daytime["condition"]);
    f.codeNight = conditionCode(nighttime["condition"]);
    if (f.codeDay == 999 && !day["iconDay"].isNull()) {
      f.codeDay = static_cast<uint16_t>(roundedOr(day["iconDay"], 999));
      f.codeNight = static_cast<uint16_t>(roundedOr(day["iconNight"], 999));
    }
    const int8_t popDay = percentOr(daytime["precipitation"]["probability"], -1);
    const int8_t popNight = percentOr(nighttime["precipitation"]["probability"], -1);
    f.precipitationPercent = popDay > popNight ? popDay : popNight;
    JsonVariantConst astro = day["astro"];
    f.sunriseMinute = localMinuteOf(astro["sunrise"]);
    f.sunsetMinute = localMinuteOf(astro["sunset"]);
    f.valid = f.maxC != app::kNoValue && f.minC != app::kNoValue;
    ++index;
  }
  if (index == 0 || !parsed[0].valid) return false;
  for (size_t i = 0; i < app::kForecastDays; ++i) out[i] = parsed[i];
  return true;
}

bool parseAir(const char* json, size_t length, app::AirQuality& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  JsonArrayConst indexes = doc["indexes"];
  if (indexes.isNull() || indexes.size() == 0) return false;
  JsonObjectConst chosen = indexes[0];
  for (JsonObjectConst index : indexes) {
    const char* code = index["code"] | "";
    if (strcmp(code, "cn-mee") == 0) {
      chosen = index;
      break;
    }
  }
  const int16_t aqi = roundedOr(chosen["aqi"], -1);
  if (aqi < 0) return false;
  out.valid = true;
  out.aqi = aqi;
  return true;
}

bool parseAlerts(const char* json, size_t length, app::WeatherAlert& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  app::WeatherAlert parsed;
  uint32_t key = 2166136261U;
  JsonArrayConst alerts = findAlertArray(root);
  uint8_t worst = 0;
  for (JsonObjectConst alert : alerts) {
    const char* message = alert["messageType"]["code"] | "";
    if (strcmp(message, "cancel") == 0) continue;
    ++parsed.count;
    key = fnv1a(key, alert["id"] | "");
    const app::AlertColor color = parseColor(alert["color"]);
    const uint8_t rank = static_cast<uint8_t>(color);
    if (!parsed.active || rank > worst) {
      worst = rank;
      parsed.active = true;
      parsed.color = color;
      const char* name = alert["eventType"]["name"].as<const char*>();
      if (name == nullptr) name = alert["typeName"].as<const char*>();
      if (name == nullptr) name = "预警";
      app::copyText(parsed.eventName, name);
    }
  }
  parsed.key = parsed.count == 0 ? 0 : key;
  out = parsed;
  return true;
}

bool parsePlace(const char* json, size_t length, app::PlaceSnapshot& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  JsonObjectConst first = doc["location"][0];
  const char* name = first["name"] | "";
  if (name[0] == '\0') return false;
  const char* city = first["adm2"] | "";
  if (city[0] == '\0' || strcmp(city, name) == 0) city = first["adm1"] | "";
  app::PlaceSnapshot parsed;
  app::copyText(parsed.name, name);
  app::copyText(parsed.city, strcmp(city, name) == 0 ? "" : city);
  parsed.valid = true;
  out = parsed;
  return true;
}

#ifdef ESP_PLATFORM

namespace {

// Collects an HTTP body into a fixed buffer; excess bytes mark overflow.
class BufferStream : public Stream {
 public:
  BufferStream(uint8_t* buffer, size_t capacity)
      : buffer_(buffer), capacity_(capacity) {}
  size_t write(uint8_t value) override {
    if (length_ >= capacity_) {
      overflow_ = true;
      return 0;
    }
    buffer_[length_++] = value;
    return 1;
  }
  size_t write(const uint8_t* data, size_t size) override {
    size_t written = 0;
    while (written < size && write(data[written]) == 1) ++written;
    return written;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t length() const { return length_; }
  bool overflow() const { return overflow_; }

 private:
  uint8_t* buffer_;
  size_t capacity_;
  size_t length_ = 0;
  bool overflow_ = false;
};

}  // namespace

WeatherClient::WeatherClient(const WeatherSettings& settings)
    : settings_(settings) {}

WeatherClient::~WeatherClient() {
  secureZero(settings_.seed, sizeof(settings_.seed));
  free(raw_);
  free(body_);
}

void WeatherClient::coordinates(char* output, size_t capacity,
                                bool lonFirst) const {
  auto fmt = [](char* out, size_t cap, int16_t centi) {
    const int value = centi < 0 ? -centi : centi;
    snprintf(out, cap, "%s%d.%02d", centi < 0 ? "-" : "", value / 100, value % 100);
  };
  char lat[12], lon[12];
  fmt(lat, sizeof(lat), settings_.latitudeCenti);
  fmt(lon, sizeof(lon), settings_.longitudeCenti);
  if (lonFirst) {
    snprintf(output, capacity, "%s,%s", lon, lat);
  } else {
    snprintf(output, capacity, "%s/%s", lat, lon);
  }
}

bool WeatherClient::makeJwt(int64_t nowEpoch, char* output,
                            size_t capacity) const {
  char header[96];
  char payload[160];
  snprintf(header, sizeof(header), "{\"alg\":\"EdDSA\",\"kid\":\"%s\"}",
           settings_.credentialId);
  const long long iat = static_cast<long long>(nowEpoch - 30);
  snprintf(payload, sizeof(payload),
           "{\"iss\":\"%s\",\"sub\":\"%s\",\"iat\":%lld,\"exp\":%lld}",
           settings_.developerId, settings_.projectId, iat, iat + 900);

  int a = base64UrlEncode(reinterpret_cast<const uint8_t*>(header),
                          strlen(header), output, capacity);
  if (a < 0 || static_cast<size_t>(a) + 2 >= capacity) return false;
  output[a++] = '.';
  const int b = base64UrlEncode(reinterpret_cast<const uint8_t*>(payload),
                                strlen(payload), output + a, capacity - a);
  if (b < 0) return false;
  const size_t signedLength = static_cast<size_t>(a + b);

  unsigned char publicKey[crypto_sign_ed25519_PUBLICKEYBYTES];
  unsigned char secretKey[crypto_sign_ed25519_SECRETKEYBYTES];
  unsigned char signature[crypto_sign_ed25519_BYTES];
  const bool keyOk = crypto_sign_ed25519_seed_keypair(publicKey, secretKey,
                                                      settings_.seed) == 0;
  const bool signedOk =
      keyOk && crypto_sign_ed25519_detached(
                   signature, nullptr,
                   reinterpret_cast<const unsigned char*>(output),
                   signedLength, secretKey) == 0;
  secureZero(secretKey, sizeof(secretKey));
  if (!signedOk || signedLength + 2 >= capacity) return false;
  output[signedLength] = '.';
  const int c = base64UrlEncode(signature, sizeof(signature),
                                output + signedLength + 1,
                                capacity - signedLength - 1);
  return c > 0;
}

app::WeatherError WeatherClient::get(const char* path, int64_t nowEpoch) {
  lastHttpCode_ = 0;
  lastTransportError_[0] = '\0';
  bodyLength_ = 0;
  // Buffers are allocated only after the TLS session is established (and
  // released before decompression) so the handshake gets the free heap.
  free(raw_);
  raw_ = nullptr;
  free(body_);
  body_ = nullptr;

  char jwt[384];
  if (!makeJwt(nowEpoch, jwt, sizeof(jwt))) return app::WeatherError::Unauthorized;

  heapBeforeConnect_ = ESP.getFreeHeap();
  NetworkClientSecure client;
  client.setCACertBundle(kCrtBundleStart, kCrtBundleEnd - kCrtBundleStart);
  client.setTimeout(kReadTimeoutMs / 1000);

  HTTPClient http;
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kReadTimeoutMs);
  http.setReuse(false);
  http.setAcceptEncoding("gzip");
  http.setUserAgent("epaper75/0.6");
  if (!http.begin(client, settings_.apiHost, 443, path, true)) {
    secureZero(jwt, sizeof(jwt));
    return app::WeatherError::Tls;
  }
  char authorization[400];
  snprintf(authorization, sizeof(authorization), "Bearer %s", jwt);
  secureZero(jwt, sizeof(jwt));
  http.addHeader("Authorization", authorization);
  secureZero(authorization, sizeof(authorization));

  const int code = http.GET();
  lastHttpCode_ = code;
  if (code <= 0) {
    client.lastError(lastTransportError_, sizeof(lastTransportError_));
    http.end();
    return app::WeatherError::Tls;
  }
  if (code == 401 || code == 403) {
    http.end();
    return app::WeatherError::Unauthorized;
  }
  if (code == 429) {
    http.end();
    return app::WeatherError::RateLimited;
  }
  if (code != 200) {
    http.end();
    return app::WeatherError::Http;
  }

  const int declared = http.getSize();
  const size_t rawCapacity =
      declared > 0 ? static_cast<size_t>(declared) : kRawLimit;
  if (rawCapacity > kRawLimit) {
    http.end();
    return app::WeatherError::Http;
  }
  raw_ = static_cast<uint8_t*>(malloc(rawCapacity));
  if (raw_ == nullptr) {
    http.end();
    return app::WeatherError::Http;
  }
  BufferStream sink(raw_, rawCapacity);
  const int streamed = http.writeToStream(&sink);
  http.end();
  const size_t rawLength = sink.length();
  if (streamed < 0 || sink.overflow() || rawLength == 0) {
    return app::WeatherError::Http;
  }

  if (looksGzip(raw_, rawLength)) {
    const uint8_t* t = raw_ + rawLength - 4;
    const size_t expected = static_cast<size_t>(t[0]) | static_cast<size_t>(t[1]) << 8 |
                            static_cast<size_t>(t[2]) << 16 | static_cast<size_t>(t[3]) << 24;
    if (expected == 0 || expected > kBodyLimit) return app::WeatherError::Decompress;
    body_ = static_cast<uint8_t*>(malloc(expected + 1));
    if (body_ == nullptr) return app::WeatherError::Decompress;
    const int inflated = gunzip(raw_, rawLength, body_, expected);
    free(raw_);
    raw_ = nullptr;
    if (inflated <= 0) return app::WeatherError::Decompress;
    bodyLength_ = static_cast<size_t>(inflated);
  } else {
    body_ = static_cast<uint8_t*>(malloc(rawLength + 1));
    if (body_ == nullptr) return app::WeatherError::Http;
    memcpy(body_, raw_, rawLength);
    free(raw_);
    raw_ = nullptr;
    bodyLength_ = rawLength;
  }
  body_[bodyLength_] = '\0';
  lastBodyLength_ = bodyLength_;
  return app::WeatherError::None;
}

app::WeatherError WeatherClient::fetchAll(int64_t nowEpoch,
                                          app::WeatherSnapshot& output) {
  if (!settings_.ready()) return app::WeatherError::NotConfigured;
  char where[32];
  char path[128];
  coordinates(where, sizeof(where), false);

  app::WeatherSnapshot next = output;
  snprintf(path, sizeof(path), "/weather/v1/current/%s?lang=zh&localTime=true", where);
  app::WeatherError error = get(path, nowEpoch);
  if (error != app::WeatherError::None) return error;
  if (!parseCurrent(reinterpret_cast<const char*>(body_), bodyLength_, next.now)) {
    return app::WeatherError::Parse;
  }

  snprintf(path, sizeof(path), "/weather/v1/daily/%s?days=7&lang=zh&localTime=true", where);
  error = get(path, nowEpoch);
  if (error != app::WeatherError::None) return error;
  if (!parseDaily(reinterpret_cast<const char*>(body_), bodyLength_, next.days)) {
    return app::WeatherError::Parse;
  }

  // Air quality and alerts are supplementary: failures keep the rest.
  snprintf(path, sizeof(path), "/airquality/v1/current/%s?lang=zh", where);
  app::AirQuality air;
  if (get(path, nowEpoch) == app::WeatherError::None &&
      parseAir(reinterpret_cast<const char*>(body_), bodyLength_, air)) {
    next.air = air;
  } else {
    next.air = app::AirQuality();
  }
  app::WeatherAlert alert;
  if (fetchAlerts(nowEpoch, alert) == app::WeatherError::None) next.alert = alert;

  next.fetchedEpoch = nowEpoch;
  next.availability = app::DataAvailability::Available;
  next.lastError = app::WeatherError::None;
  output = next;
  return app::WeatherError::None;
}

app::WeatherError WeatherClient::fetchAlerts(int64_t nowEpoch,
                                             app::WeatherAlert& output) {
  if (!settings_.ready()) return app::WeatherError::NotConfigured;
  char where[32];
  char path[128];
  coordinates(where, sizeof(where), false);
  snprintf(path, sizeof(path), "/weatheralert/v1/current/%s?lang=zh&localTime=true", where);
  const app::WeatherError error = get(path, nowEpoch);
  if (error != app::WeatherError::None) return error;
  app::WeatherAlert parsed;
  if (!parseAlerts(reinterpret_cast<const char*>(body_), bodyLength_, parsed)) {
    return app::WeatherError::Parse;
  }
  output = parsed;
  return app::WeatherError::None;
}

app::WeatherError WeatherClient::lookupPlace(int64_t nowEpoch,
                                             app::PlaceSnapshot& output) {
  if (!settings_.ready()) return app::WeatherError::NotConfigured;
  char where[32];
  char path[128];
  coordinates(where, sizeof(where), true);
  snprintf(path, sizeof(path), "/geo/v2/city/lookup?location=%s&number=1&lang=zh", where);
  const app::WeatherError error = get(path, nowEpoch);
  if (error != app::WeatherError::None) return error;
  return parsePlace(reinterpret_cast<const char*>(body_), bodyLength_, output)
             ? app::WeatherError::None
             : app::WeatherError::Parse;
}

#endif  // ESP_PLATFORM

}  // namespace net
