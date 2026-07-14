#pragma once

#include <Arduino.h>

#include "Buttons.h"

namespace board {

enum class Sht30Error : uint8_t {
  None = 0,
  BusInitFailed,
  CommandFailed,
  ShortRead,
  TemperatureCrc,
  HumidityCrc,
};

const char* sht30ErrorName(Sht30Error error);

struct EnvironmentReading {
  bool ok = false;
  float temperatureC = NAN;
  float humidityPercent = NAN;
  Sht30Error error = Sht30Error::None;
};

struct BatteryReading {
  bool ok = false;
  uint16_t adcMillivolts = 0;
  uint16_t batteryMillivolts = 0;
  int8_t batteryPercent = -1;
};

// Estimates state-of-charge for a conventional 4.2 V single-cell Li-ion/LiPo
// battery from open-circuit voltage. This is approximate, not a fuel gauge.
int8_t estimateBatteryPercent(uint16_t batteryMillivolts);

struct TelemetryReading {
  EnvironmentReading environment;
  BatteryReading battery;
  uint32_t sampledAtMs = 0;
};

class Board {
 public:
  // Establishes safe output levels before changing pin direction. begin() is
  // idempotent and leaves GPIO25 LOW.
  void begin();

  Buttons& buttons() { return buttons_; }
  const Buttons& buttons() const { return buttons_; }

  // These methods are synchronous hardware operations (roughly 50-120 ms).
  // Invoke them from the Arduino loop, never from an HTTP callback/task.
  EnvironmentReading readEnvironment();
  BatteryReading readBattery();
  uint16_t readBatteryMillivolts();
  TelemetryReading sampleTelemetry();

 private:
  void ensureBegun();

  Buttons buttons_;
  bool begun_ = false;
};

}  // namespace board
