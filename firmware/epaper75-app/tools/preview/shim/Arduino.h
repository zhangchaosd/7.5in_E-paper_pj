// Minimal host shim so Adafruit GFX and the renderer compile on macOS/Linux.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
typedef uint8_t byte;
typedef bool boolean;
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#define pgm_read_word(a) (*(const uint16_t*)(a))
#define pgm_read_dword(a) (*(const uint32_t*)(a))
#define pgm_read_pointer(a) (*(void* const*)(a))
#define LOW 0
#define HIGH 1
template <class T, class U> auto min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template <class T, class U> auto max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
class __FlashStringHelper;
#include <string>
class String {
 public:
  String(const char* s = "") : s_(s ? s : "") {}
  const char* c_str() const { return s_.c_str(); }
  unsigned int length() const { return s_.size(); }
  char operator[](unsigned int i) const { return s_[i]; }
  void toCharArray(char* buf, unsigned int n) const { strncpy(buf, s_.c_str(), n); if (n) buf[n - 1] = 0; }
 private:
  std::string s_;
};
#define radians(d) ((d) * 0.017453292519943295)
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t r = 0; while (n--) r += write(*b++); return r; }
  size_t print(const char* s) { return write(reinterpret_cast<const uint8_t*>(s), strlen(s)); }
};
