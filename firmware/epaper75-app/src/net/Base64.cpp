#include "net/Base64.h"

#include <string.h>

namespace net {
namespace {

int decodeChar(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

}  // namespace

int base64Decode(const char* input, uint8_t* output, size_t capacity) {
  if (input == nullptr) return -1;
  size_t length = strlen(input);
  while (length > 0 && input[length - 1] == '=') --length;
  if (length % 4 == 1) return -1;

  size_t written = 0;
  uint32_t buffer = 0;
  int bits = 0;
  for (size_t i = 0; i < length; ++i) {
    const int value = decodeChar(input[i]);
    if (value < 0) return -1;
    buffer = (buffer << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (written >= capacity) return -1;
      output[written++] = static_cast<uint8_t>((buffer >> bits) & 0xffU);
    }
  }
  return static_cast<int>(written);
}

int base64UrlEncode(const uint8_t* input, size_t length, char* output,
                    size_t capacity) {
  static const char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  const size_t needed = (length * 4 + 2) / 3;
  if (needed + 1 > capacity) return -1;
  size_t out = 0;
  size_t i = 0;
  while (i + 3 <= length) {
    const uint32_t v = (static_cast<uint32_t>(input[i]) << 16) |
                       (static_cast<uint32_t>(input[i + 1]) << 8) | input[i + 2];
    output[out++] = kAlphabet[(v >> 18) & 63];
    output[out++] = kAlphabet[(v >> 12) & 63];
    output[out++] = kAlphabet[(v >> 6) & 63];
    output[out++] = kAlphabet[v & 63];
    i += 3;
  }
  const size_t rest = length - i;
  if (rest == 1) {
    const uint32_t v = static_cast<uint32_t>(input[i]) << 16;
    output[out++] = kAlphabet[(v >> 18) & 63];
    output[out++] = kAlphabet[(v >> 12) & 63];
  } else if (rest == 2) {
    const uint32_t v = (static_cast<uint32_t>(input[i]) << 16) |
                       (static_cast<uint32_t>(input[i + 1]) << 8);
    output[out++] = kAlphabet[(v >> 18) & 63];
    output[out++] = kAlphabet[(v >> 12) & 63];
    output[out++] = kAlphabet[(v >> 6) & 63];
  }
  output[out] = '\0';
  return static_cast<int>(out);
}

}  // namespace net
