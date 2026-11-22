// Host build of the pure parsers in src/net/SettingsStore.cpp (generated
// slice: keep in sync by rerunning the extraction in run_host_tests.sh).
#include "net/SettingsStore.h"
#include <ctype.h>
#include <string.h>
#include "net/Base64.h"
namespace net {
namespace {
constexpr uint8_t kPkcs8Prefix[16] = {0x30, 0x2e, 0x02, 0x01, 0x00, 0x30,
                                      0x05, 0x06, 0x03, 0x2b, 0x65, 0x70,
                                      0x04, 0x22, 0x04, 0x20};
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

}  // namespace net
