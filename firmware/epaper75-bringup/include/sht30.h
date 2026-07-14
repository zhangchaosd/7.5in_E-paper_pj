#pragma once

#include <Arduino.h>
#include <Wire.h>

enum class Sht30Error : uint8_t {
  kNone = 0,
  kCommandFailed,
  kShortRead,
  kTemperatureCrc,
  kHumidityCrc,
};

struct Sht30Reading {
  bool ok = false;
  float temperatureC = NAN;
  float humidityPercent = NAN;
  Sht30Error error = Sht30Error::kNone;
};

Sht30Reading readSht30(TwoWire& wire, uint8_t address);
const char* sht30ErrorName(Sht30Error error);
