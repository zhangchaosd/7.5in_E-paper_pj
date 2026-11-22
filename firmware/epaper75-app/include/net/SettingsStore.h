#pragma once

#include <Arduino.h>

#include "app/Models.h"

namespace net {

constexpr size_t kApiHostSize = 64;
constexpr size_t kQWeatherIdSize = 24;
constexpr size_t kEd25519SeedSize = 32;

// QWeather JWT credentials and the forecast location. These never enter
// UiModel, serial output, or the renderer. Coordinates are stored as
// hundredths of a degree (QWeather accepts at most two decimals).
struct WeatherSettings {
  char apiHost[kApiHostSize] = {};          // e.g. abcxyz.qweatherapi.com
  char developerId[kQWeatherIdSize] = {};   // JWT iss
  char projectId[kQWeatherIdSize] = {};     // JWT sub
  char credentialId[kQWeatherIdSize] = {};  // JWT kid
  uint8_t seed[kEd25519SeedSize] = {};      // Ed25519 private key seed
  bool hasCredentials = false;
  int16_t latitudeCenti = 0;   // -9000..9000
  int16_t longitudeCenti = 0;  // -18000..18000
  bool hasLocation = false;

  bool ready() const { return hasCredentials && hasLocation; }
};

// Cached GeoAPI result, keyed by the coordinates it was resolved for.
struct PlaceCache {
  app::PlaceSnapshot place;
  int16_t latitudeCenti = 0;
  int16_t longitudeCenti = 0;
};

bool validateApiHost(const char* host);
bool validateQWeatherId(const char* id);
// Accepts the base64 body of a PKCS#8 Ed25519 PEM (48 bytes decoded) or a
// raw base64 32-byte seed. Writes the seed only when the whole input is valid.
bool parseEd25519PrivateKey(const char* base64, uint8_t (&seed)[kEd25519SeedSize]);
// Parses "39.92" style decimal degrees into hundredths, rounding half away.
bool parseCoordinate(const char* text, int16_t limitCenti, int16_t& output);

void secureZero(void* memory, size_t size);

// Uses the application-owned NVS namespace only. Never touches the Wi-Fi
// driver's namespace. All calls happen on the Arduino loop task.
class SettingsStore {
 public:
  bool loadWeather(WeatherSettings& output);
  bool saveCredentials(const WeatherSettings& credentials);
  bool saveLocation(int16_t latitudeCenti, int16_t longitudeCenti);
  bool clearWeather();

  bool loadPlace(PlaceCache& output);
  bool savePlace(const PlaceCache& place);
};

}  // namespace net
