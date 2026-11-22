#pragma once

#include <stddef.h>
#include <stdint.h>

namespace net {

// Inflates a complete gzip member (RFC 1952) into output using the ESP32 ROM
// tinfl decoder. Returns the decompressed length, or -1 on a malformed stream
// or if the output buffer is too small. The CRC32 trailer is verified.
int gunzip(const uint8_t* input, size_t length, uint8_t* output,
           size_t capacity);

bool looksGzip(const uint8_t* input, size_t length);

}  // namespace net
