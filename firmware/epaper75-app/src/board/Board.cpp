#include "board/Board.h"

#include <Wire.h>
#include <driver/gpio.h>

#include "board/BoardConfig.h"

namespace board {
namespace {

constexpr uint32_t kI2cFrequencyHz = 100000;
constexpr uint16_t kI2cTimeoutMs = 80;
constexpr uint8_t kAdcWarmupSamples = 8;
constexpr uint8_t kAdcSamples = 32;
constexpr uint8_t kPeripheralSettleMs = 15;

void latchThenOutput(uint8_t pin, uint8_t level) {
  // Arduino-ESP32 3.x reports an error when digitalWrite() precedes pinMode().
  // Seed the hardware latch first so switching direction cannot glitch GPIO25
  // or the e-paper control signals.
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
}

void setPeripheralEnabled(bool enabled) {
  digitalWrite(kPeripheralEnable, enabled ? HIGH : LOW);
  if (enabled) {
    delay(kPeripheralSettleMs);
  }
}

class ScopedPeripheralGate {
 public:
  ScopedPeripheralGate() { setPeripheralEnabled(true); }
  ~ScopedPeripheralGate() { setPeripheralEnabled(false); }

  ScopedPeripheralGate(const ScopedPeripheralGate&) = delete;
  ScopedPeripheralGate& operator=(const ScopedPeripheralGate&) = delete;
};

class ScopedI2cSession {
 public:
  ScopedI2cSession() {
    setPeripheralEnabled(true);
    started_ = Wire.begin(kI2cSda, kI2cScl, kI2cFrequencyHz);
    if (started_) {
      Wire.setTimeOut(kI2cTimeoutMs);
    }
  }

  ~ScopedI2cSession() {
    if (started_) {
      Wire.end();
    }
    // Do not leave either I2C line driving an unpowered SHT30.
    pinMode(kI2cSda, INPUT);
    pinMode(kI2cScl, INPUT);
    setPeripheralEnabled(false);
  }

  bool started() const { return started_; }

  ScopedI2cSession(const ScopedI2cSession&) = delete;
  ScopedI2cSession& operator=(const ScopedI2cSession&) = delete;

 private:
  bool started_ = false;
};

uint8_t sht30Crc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1U) ^ 0x31U)
                          : static_cast<uint8_t>(crc << 1U);
    }
  }
  return crc;
}

uint16_t roundedAndClampedMillivolts(float value) {
  if (value <= 0.0f) {
    return 0;
  }
  if (value >= 65535.0f) {
    return 65535;
  }
  return static_cast<uint16_t>(value + 0.5f);
}

struct BatterySocPoint {
  uint16_t millivolts;
  uint8_t percent;
};

constexpr BatterySocPoint kBatterySocCurve[] = {
    {4200, 100}, {4150, 95}, {4100, 90}, {4050, 85}, {4000, 75},
    {3950, 65},  {3900, 55}, {3850, 45}, {3800, 35}, {3750, 25},
    {3700, 15},  {3650, 10}, {3600, 5},  {3500, 1},  {3400, 0},
};

}  // namespace

int8_t estimateBatteryPercent(uint16_t batteryMillivolts) {
  if (batteryMillivolts < 500) return -1;
  if (batteryMillivolts >= kBatterySocCurve[0].millivolts) return 100;

  constexpr size_t count =
      sizeof(kBatterySocCurve) / sizeof(kBatterySocCurve[0]);
  if (batteryMillivolts <= kBatterySocCurve[count - 1].millivolts) return 0;

  for (size_t i = 0; i + 1 < count; ++i) {
    const BatterySocPoint high = kBatterySocCurve[i];
    const BatterySocPoint low = kBatterySocCurve[i + 1];
    if (batteryMillivolts < low.millivolts) continue;

    const uint32_t voltageSpan = high.millivolts - low.millivolts;
    const uint32_t percentSpan = high.percent - low.percent;
    const uint32_t offset = batteryMillivolts - low.millivolts;
    return static_cast<int8_t>(
        low.percent + (offset * percentSpan + voltageSpan / 2U) / voltageSpan);
  }
  return 0;
}

const char* sht30ErrorName(Sht30Error error) {
  switch (error) {
    case Sht30Error::None:
      return "none";
    case Sht30Error::BusInitFailed:
      return "bus_init_failed";
    case Sht30Error::CommandFailed:
      return "command_failed";
    case Sht30Error::ShortRead:
      return "short_read";
    case Sht30Error::TemperatureCrc:
      return "temperature_crc";
    case Sht30Error::HumidityCrc:
      return "humidity_crc";
  }
  return "unknown";
}

void Board::begin() {
  if (begun_) {
    return;
  }

  latchThenOutput(kPeripheralEnable, LOW);
  latchThenOutput(kEpdCs, HIGH);
  latchThenOutput(kEpdReset, HIGH);
  latchThenOutput(kEpdDc, HIGH);

  // SPI owns SCK/MOSI only while the renderer is active. The remaining pins
  // are deliberately high impedance at idle.
  pinMode(kEpdSck, INPUT);
  pinMode(kEpdMosi, INPUT);
  pinMode(kEpdBusy, INPUT);
  pinMode(kBatteryAdc, INPUT);
  pinMode(kI2cSda, INPUT);
  pinMode(kI2cScl, INPUT);

  buttons_.begin(millis());
  begun_ = true;
}

EnvironmentReading Board::readEnvironment() {
  ensureBegun();
  EnvironmentReading result;
  ScopedI2cSession session;
  if (!session.started()) {
    result.error = Sht30Error::BusInitFailed;
    return result;
  }

  // Single-shot, high repeatability, clock stretching disabled.
  Wire.beginTransmission(kSht30Address);
  Wire.write(0x24);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) {
    result.error = Sht30Error::CommandFailed;
    return result;
  }

  delay(20);
  constexpr uint8_t kExpectedBytes = 6;
  const size_t received = Wire.requestFrom(kSht30Address, kExpectedBytes);
  if (received != kExpectedBytes) {
    while (Wire.available()) {
      Wire.read();
    }
    result.error = Sht30Error::ShortRead;
    return result;
  }

  uint8_t bytes[kExpectedBytes];
  for (uint8_t& value : bytes) {
    value = static_cast<uint8_t>(Wire.read());
  }

  if (sht30Crc8(bytes, 2) != bytes[2]) {
    result.error = Sht30Error::TemperatureCrc;
    return result;
  }
  if (sht30Crc8(bytes + 3, 2) != bytes[5]) {
    result.error = Sht30Error::HumidityCrc;
    return result;
  }

  const uint16_t rawTemperature = static_cast<uint16_t>(
      (static_cast<uint16_t>(bytes[0]) << 8U) | bytes[1]);
  const uint16_t rawHumidity = static_cast<uint16_t>(
      (static_cast<uint16_t>(bytes[3]) << 8U) | bytes[4]);

  result.temperatureC =
      -45.0f + (175.0f * static_cast<float>(rawTemperature) / 65535.0f);
  result.humidityPercent =
      100.0f * static_cast<float>(rawHumidity) / 65535.0f;
  result.error = Sht30Error::None;
  result.ok = true;
  return result;
}

BatteryReading Board::readBattery() {
  ensureBegun();
  ScopedPeripheralGate gate;

  analogReadResolution(12);

  // In Arduino-ESP32 3.x the first conversion creates the per-pin ADC
  // channel. Applying attenuation before this conversion produces an error.
  analogRead(kBatteryAdc);
  analogSetPinAttenuation(kBatteryAdc, ADC_11db);

  for (uint8_t i = 0; i < kAdcWarmupSamples; ++i) {
    analogReadMilliVolts(kBatteryAdc);
    delay(2);
  }

  uint32_t sumMillivolts = 0;
  for (uint8_t i = 0; i < kAdcSamples; ++i) {
    sumMillivolts += analogReadMilliVolts(kBatteryAdc);
    delay(2);
  }

  BatteryReading result;
  const float adcMillivolts =
      static_cast<float>(sumMillivolts) / static_cast<float>(kAdcSamples);
  result.adcMillivolts = roundedAndClampedMillivolts(adcMillivolts);
  result.batteryMillivolts = roundedAndClampedMillivolts(
      adcMillivolts * kBatteryDividerRatio);
  result.batteryPercent = estimateBatteryPercent(result.batteryMillivolts);
  result.ok = true;
  return result;
}

uint16_t Board::readBatteryMillivolts() {
  return readBattery().batteryMillivolts;
}

TelemetryReading Board::sampleTelemetry() {
  TelemetryReading result;
  result.environment = readEnvironment();
  result.battery = readBattery();
  result.sampledAtMs = millis();
  return result;
}

void Board::ensureBegun() {
  if (!begun_) {
    begin();
  }
}

}  // namespace board
