#include "net/SettingsStore.h"

#include <Preferences.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "net/Base64.h"

namespace net {
namespace {

constexpr char kNamespace[] = "ep75_p1";
constexpr char kCredentialsKey[] = "qw_cred";
constexpr char kLocationKey[] = "qw_loc";
constexpr char kPlaceKey[] = "qw_place";

constexpr uint8_t kCredentialMagic[4] = {'E', 'P', 'Q', 'W'};
constexpr uint16_t kCredentialVersion = 1;
constexpr size_t kCredentialPayload =
    kApiHostSize + 3 * kQWeatherIdSize + kEd25519SeedSize;
constexpr size_t kCredentialBlob = 12 + kCredentialPayload;

constexpr uint8_t kPlaceMagic[4] = {'E', 'P', 'P', 'L'};
constexpr size_t kPlacePayload = 4 + 2 * app::kPlaceNameSize;
constexpr size_t kPlaceBlob = 12 + kPlacePayload;

// RFC 8410 PKCS#8 prefix for an Ed25519 private key (16 bytes).
constexpr uint8_t kPkcs8Prefix[16] = {0x30, 0x2e, 0x02, 0x01, 0x00, 0x30,
                                      0x05, 0x06, 0x03, 0x2b, 0x65, 0x70,
                                      0x04, 0x22, 0x04, 0x20};

uint32_t crc32(const uint8_t* data, size_t length, uint32_t crc = 0xffffffffU) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
  }
  return crc;
}

void writeU16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
uint16_t readU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
void writeU32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
         static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

// Header: magic[4] version[2] payloadLength[2] crc32[4] over header[0..8)+payload.
void sealBlob(uint8_t* blob, const uint8_t (&magic)[4], uint16_t version,
              size_t payload) {
  memcpy(blob, magic, 4);
  writeU16(blob + 4, version);
  writeU16(blob + 6, static_cast<uint16_t>(payload));
  uint32_t crc = crc32(blob, 8);
  crc = crc32(blob + 12, payload, crc) ^ 0xffffffffU;
  writeU32(blob + 8, crc);
}

bool blobValid(const uint8_t* blob, const uint8_t (&magic)[4],
               uint16_t version, size_t payload) {
  if (memcmp(blob, magic, 4) != 0 || readU16(blob + 4) != version ||
      readU16(blob + 6) != payload) {
    return false;
  }
  uint32_t crc = crc32(blob, 8);
  crc = crc32(blob + 12, payload, crc) ^ 0xffffffffU;
  return crc == readU32(blob + 8);
}

bool terminated(const char* text, size_t size) {
  return memchr(text, '\0', size) != nullptr;
}

}  // namespace

void secureZero(void* memory, size_t size) {
  volatile uint8_t* cursor = static_cast<volatile uint8_t*>(memory);
  while (size-- > 0) *cursor++ = 0;
}

bool validateApiHost(const char* host) {
  if (host == nullptr) return false;
  const size_t length = strlen(host);
  if (length < 4 || length >= kApiHostSize || host[0] == '.' ||
      host[length - 1] == '.' || strchr(host, '.') == nullptr) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(host[i]);
    if (!(isalnum(c) || c == '-' || c == '.')) return false;
  }
  return true;
}

bool validateQWeatherId(const char* id) {
  if (id == nullptr) return false;
  const size_t length = strlen(id);
  if (length < 4 || length >= kQWeatherIdSize) return false;
  for (size_t i = 0; i < length; ++i) {
    if (!isalnum(static_cast<unsigned char>(id[i]))) return false;
  }
  return true;
}

bool parseEd25519PrivateKey(const char* base64,
                            uint8_t (&seed)[kEd25519SeedSize]) {
  uint8_t decoded[64] = {};
  const int length = base64Decode(base64, decoded, sizeof(decoded));
  bool ok = false;
  if (length == 48 && memcmp(decoded, kPkcs8Prefix, sizeof(kPkcs8Prefix)) == 0) {
    memcpy(seed, decoded + 16, kEd25519SeedSize);
    ok = true;
  } else if (length == 32) {
    memcpy(seed, decoded, kEd25519SeedSize);
    ok = true;
  }
  secureZero(decoded, sizeof(decoded));
  return ok;
}

bool parseCoordinate(const char* text, int16_t limitCenti, int16_t& output) {
  if (text == nullptr || *text == '\0') return false;
  const char* p = text;
  bool negative = false;
  if (*p == '-' || *p == '+') negative = *p++ == '-';
  int32_t whole = 0;
  int digits = 0;
  while (isdigit(static_cast<unsigned char>(*p))) {
    whole = whole * 10 + (*p++ - '0');
    if (++digits > 3) return false;
  }
  if (digits == 0) return false;
  int32_t fraction = 0;  // in thousandths for rounding
  if (*p == '.') {
    ++p;
    int places = 0;
    while (isdigit(static_cast<unsigned char>(*p))) {
      if (places < 3) fraction = fraction * 10 + (*p - '0');
      ++places;
      ++p;
    }
    if (places == 0) return false;
    for (int i = places; i < 3; ++i) fraction *= 10;
  }
  if (*p != '\0') return false;
  int32_t centi = whole * 100 + (fraction + 5) / 10;
  if (centi > limitCenti) return false;
  output = static_cast<int16_t>(negative ? -centi : centi);
  return true;
}

bool SettingsStore::loadWeather(WeatherSettings& output) {
  WeatherSettings loaded;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    output = loaded;
    return false;
  }
  if (prefs.getBytesLength(kCredentialsKey) == kCredentialBlob) {
    uint8_t blob[kCredentialBlob] = {};
    if (prefs.getBytes(kCredentialsKey, blob, sizeof(blob)) == sizeof(blob) &&
        blobValid(blob, kCredentialMagic, kCredentialVersion,
                  kCredentialPayload)) {
      size_t o = 12;
      memcpy(loaded.apiHost, blob + o, kApiHostSize); o += kApiHostSize;
      memcpy(loaded.developerId, blob + o, kQWeatherIdSize); o += kQWeatherIdSize;
      memcpy(loaded.projectId, blob + o, kQWeatherIdSize); o += kQWeatherIdSize;
      memcpy(loaded.credentialId, blob + o, kQWeatherIdSize); o += kQWeatherIdSize;
      memcpy(loaded.seed, blob + o, kEd25519SeedSize);
      loaded.hasCredentials =
          terminated(loaded.apiHost, kApiHostSize) &&
          terminated(loaded.developerId, kQWeatherIdSize) &&
          terminated(loaded.projectId, kQWeatherIdSize) &&
          terminated(loaded.credentialId, kQWeatherIdSize) &&
          validateApiHost(loaded.apiHost) &&
          validateQWeatherId(loaded.developerId) &&
          validateQWeatherId(loaded.projectId) &&
          validateQWeatherId(loaded.credentialId);
      if (!loaded.hasCredentials) secureZero(loaded.seed, sizeof(loaded.seed));
    }
    secureZero(blob, sizeof(blob));
  }
  const uint32_t location = prefs.getUInt(kLocationKey, 0xffffffffU);
  if (location != 0xffffffffU) {
    const int16_t lat = static_cast<int16_t>(location >> 16);
    const int16_t lon = static_cast<int16_t>(location & 0xffffU);
    if (lat >= -9000 && lat <= 9000 && lon >= -18000 && lon <= 18000) {
      loaded.latitudeCenti = lat;
      loaded.longitudeCenti = lon;
      loaded.hasLocation = true;
    }
  }
  prefs.end();
  output = loaded;
  secureZero(loaded.seed, sizeof(loaded.seed));
  return true;
}

bool SettingsStore::saveCredentials(const WeatherSettings& c) {
  if (!validateApiHost(c.apiHost) || !validateQWeatherId(c.developerId) ||
      !validateQWeatherId(c.projectId) || !validateQWeatherId(c.credentialId)) {
    return false;
  }
  uint8_t blob[kCredentialBlob] = {};
  size_t o = 12;
  memcpy(blob + o, c.apiHost, kApiHostSize); o += kApiHostSize;
  memcpy(blob + o, c.developerId, kQWeatherIdSize); o += kQWeatherIdSize;
  memcpy(blob + o, c.projectId, kQWeatherIdSize); o += kQWeatherIdSize;
  memcpy(blob + o, c.credentialId, kQWeatherIdSize); o += kQWeatherIdSize;
  memcpy(blob + o, c.seed, kEd25519SeedSize);
  sealBlob(blob, kCredentialMagic, kCredentialVersion, kCredentialPayload);

  Preferences prefs;
  bool saved = false;
  if (prefs.begin(kNamespace, false)) {
    saved = prefs.putBytes(kCredentialsKey, blob, sizeof(blob)) == sizeof(blob);
    prefs.end();
  }
  secureZero(blob, sizeof(blob));
  return saved;
}

bool SettingsStore::saveLocation(int16_t lat, int16_t lon) {
  if (lat < -9000 || lat > 9000 || lon < -18000 || lon > 18000) return false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const uint32_t packed = (static_cast<uint32_t>(static_cast<uint16_t>(lat)) << 16) |
                          static_cast<uint16_t>(lon);
  const bool saved = prefs.putUInt(kLocationKey, packed) == sizeof(uint32_t);
  prefs.remove(kPlaceKey);  // the cached place name no longer applies
  prefs.end();
  return saved;
}

bool SettingsStore::clearWeather() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  prefs.remove(kCredentialsKey);
  prefs.remove(kLocationKey);
  prefs.remove(kPlaceKey);
  prefs.end();
  return true;
}

bool SettingsStore::loadPlace(PlaceCache& output) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return false;
  bool ok = false;
  if (prefs.getBytesLength(kPlaceKey) == kPlaceBlob) {
    uint8_t blob[kPlaceBlob] = {};
    if (prefs.getBytes(kPlaceKey, blob, sizeof(blob)) == sizeof(blob) &&
        blobValid(blob, kPlaceMagic, 1, kPlacePayload)) {
      PlaceCache cache;
      cache.latitudeCenti = static_cast<int16_t>(readU16(blob + 12));
      cache.longitudeCenti = static_cast<int16_t>(readU16(blob + 14));
      memcpy(cache.place.name, blob + 16, app::kPlaceNameSize);
      memcpy(cache.place.city, blob + 16 + app::kPlaceNameSize, app::kPlaceNameSize);
      cache.place.name[app::kPlaceNameSize - 1] = '\0';
      cache.place.city[app::kPlaceNameSize - 1] = '\0';
      cache.place.valid = cache.place.name[0] != '\0';
      output = cache;
      ok = cache.place.valid;
    }
  }
  prefs.end();
  return ok;
}

bool SettingsStore::savePlace(const PlaceCache& cache) {
  uint8_t blob[kPlaceBlob] = {};
  writeU16(blob + 12, static_cast<uint16_t>(cache.latitudeCenti));
  writeU16(blob + 14, static_cast<uint16_t>(cache.longitudeCenti));
  memcpy(blob + 16, cache.place.name, app::kPlaceNameSize);
  memcpy(blob + 16 + app::kPlaceNameSize, cache.place.city, app::kPlaceNameSize);
  sealBlob(blob, kPlaceMagic, 1, kPlacePayload);
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const bool saved = prefs.putBytes(kPlaceKey, blob, sizeof(blob)) == sizeof(blob);
  prefs.end();
  return saved;
}

}  // namespace net
