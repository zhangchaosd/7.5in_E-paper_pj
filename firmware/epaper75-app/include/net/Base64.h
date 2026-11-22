#pragma once

#include <stddef.h>
#include <stdint.h>

namespace net {

// Standard base64 (RFC 4648 section 4) decoding; '=' padding optional,
// whitespace rejected. Returns decoded length, or -1 on malformed input or
// insufficient capacity.
int base64Decode(const char* input, uint8_t* output, size_t capacity);

// Unpadded base64url (RFC 4648 section 5) encoding, as JWT requires. Returns
// the number of characters written (excluding NUL) or -1 if it does not fit.
int base64UrlEncode(const uint8_t* input, size_t length, char* output,
                    size_t capacity);

}  // namespace net
