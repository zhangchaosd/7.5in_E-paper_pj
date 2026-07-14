#pragma once

#include <Arduino.h>

namespace board {

constexpr uint32_t kSerialBaud = 115200;

// GDEW075Z08-compatible 7.5 inch, 800x480, black/white/red panel.
constexpr uint8_t kEpdSck = 18;
constexpr uint8_t kEpdMosi = 23;
constexpr uint8_t kEpdCs = 5;
constexpr uint8_t kEpdDc = 19;
constexpr uint8_t kEpdReset = 16;
constexpr uint8_t kEpdBusy = 17;
constexpr uint16_t kEpdPageHeight = 64;
constexpr uint32_t kEpdBusyTimeoutMs = 45000;
constexpr uint32_t kEpdStageTimeoutMs = 50000;

// GPIO25 enables both the SHT30 low-side ground switch and the battery divider.
// It is not an ESP32 power-hold signal.
constexpr uint8_t kPeripheralEnable = 25;
constexpr uint8_t kI2cSda = 21;
constexpr uint8_t kI2cScl = 22;
constexpr uint8_t kSht30Address = 0x44;
constexpr uint8_t kBatteryAdc = 36;
constexpr float kBatteryDividerRatio = 2.0f;

// These pins are input-only on ESP32 and have external 10k pull-ups on the PCB.
// Physical order from the SHT30/left edge toward USB-C/right edge is:
// KEY3 (GPIO39), KEY2 (GPIO35), KEY1 (GPIO34), EN reset.
constexpr uint8_t kKey1 = 34;
constexpr uint8_t kKey2 = 35;
constexpr uint8_t kKey3 = 39;
constexpr uint8_t kKeyPressedLevel = LOW;

}  // namespace board
