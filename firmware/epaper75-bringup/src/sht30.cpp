#include "../include/sht30.h"

namespace {

uint8_t crc8(const uint8_t* data, size_t length) {
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

}  // namespace

Sht30Reading readSht30(TwoWire& wire, uint8_t address) {
  Sht30Reading result;

  // High repeatability, no clock stretching.
  wire.beginTransmission(address);
  wire.write(0x24);
  wire.write(0x00);
  if (wire.endTransmission() != 0) {
    result.error = Sht30Error::kCommandFailed;
    return result;
  }

  delay(20);
  const uint8_t requested = 6;
  const uint8_t received = wire.requestFrom(address, requested);
  if (received != requested) {
    while (wire.available()) {
      wire.read();
    }
    result.error = Sht30Error::kShortRead;
    return result;
  }

  uint8_t bytes[6];
  for (uint8_t& value : bytes) {
    value = static_cast<uint8_t>(wire.read());
  }

  if (crc8(bytes, 2) != bytes[2]) {
    result.error = Sht30Error::kTemperatureCrc;
    return result;
  }
  if (crc8(bytes + 3, 2) != bytes[5]) {
    result.error = Sht30Error::kHumidityCrc;
    return result;
  }

  const uint16_t rawTemperature =
      static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8U) | bytes[1]);
  const uint16_t rawHumidity =
      static_cast<uint16_t>((static_cast<uint16_t>(bytes[3]) << 8U) | bytes[4]);

  result.temperatureC = -45.0f + (175.0f * rawTemperature / 65535.0f);
  result.humidityPercent = 100.0f * rawHumidity / 65535.0f;
  result.error = Sht30Error::kNone;
  result.ok = true;
  return result;
}

const char* sht30ErrorName(Sht30Error error) {
  switch (error) {
    case Sht30Error::kNone:
      return "none";
    case Sht30Error::kCommandFailed:
      return "command_failed";
    case Sht30Error::kShortRead:
      return "short_read";
    case Sht30Error::kTemperatureCrc:
      return "temperature_crc";
    case Sht30Error::kHumidityCrc:
      return "humidity_crc";
  }
  return "unknown";
}
