#include "net/Gzip.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "miniz.h"
#endif

namespace net {
namespace {

constexpr uint8_t kFlagHcrc = 0x02;
constexpr uint8_t kFlagExtra = 0x04;
constexpr uint8_t kFlagName = 0x08;
constexpr uint8_t kFlagComment = 0x10;

uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
  }
  return crc ^ 0xffffffffU;
}

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
         static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

}  // namespace

bool looksGzip(const uint8_t* input, size_t length) {
  return length >= 18 && input[0] == 0x1f && input[1] == 0x8b && input[2] == 8;
}

int gunzip(const uint8_t* input, size_t length, uint8_t* output,
           size_t capacity) {
#ifdef ESP_PLATFORM
  if (!looksGzip(input, length)) return -1;
  const uint8_t flags = input[3];
  size_t offset = 10;
  if (flags & kFlagExtra) {
    if (offset + 2 > length) return -1;
    offset += 2 + (input[offset] | (input[offset + 1] << 8));
  }
  if (flags & kFlagName) {
    while (offset < length && input[offset] != 0) ++offset;
    ++offset;
  }
  if (flags & kFlagComment) {
    while (offset < length && input[offset] != 0) ++offset;
    ++offset;
  }
  if (flags & kFlagHcrc) offset += 2;
  if (offset + 8 > length) return -1;

  const size_t deflateLength = length - offset - 8;
  const uint32_t expectedCrc = readLe32(input + length - 8);
  const uint32_t expectedSize = readLe32(input + length - 4);
  if (expectedSize > capacity) return -1;

  tinfl_decompressor* decompressor =
      static_cast<tinfl_decompressor*>(malloc(sizeof(tinfl_decompressor)));
  if (decompressor == nullptr) return -1;
  tinfl_init(decompressor);
  size_t inSize = deflateLength;
  size_t outSize = capacity;
  const tinfl_status status = tinfl_decompress(
      decompressor, input + offset, &inSize, output, output, &outSize,
      TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  free(decompressor);
  if (status != TINFL_STATUS_DONE || outSize != expectedSize) return -1;
  if (crc32(output, outSize) != expectedCrc) return -1;
  return static_cast<int>(outSize);
#else
  (void)input;
  (void)length;
  (void)output;
  (void)capacity;
  return -1;
#endif
}

}  // namespace net
