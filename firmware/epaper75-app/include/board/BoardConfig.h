#pragma once

#include <Arduino.h>

namespace board {

constexpr char kFirmwareVersion[] = "0.3.0-p1";
constexpr uint32_t kSerialBaud = 115200;

// 7.5-inch 800x480 black/white/red panel.  The flex is marked
// P750057-MF1-A and the proven compatible GxEPD2 driver is 750c_Z08.
constexpr uint8_t kEpdSck = 18;
constexpr uint8_t kEpdMosi = 23;
constexpr uint8_t kEpdCs = 5;
constexpr uint8_t kEpdDc = 19;
constexpr uint8_t kEpdReset = 16;
constexpr uint8_t kEpdBusy = 17;
constexpr uint16_t kEpdWidth = 800;
constexpr uint16_t kEpdHeight = 480;
constexpr uint16_t kEpdPageHeight = 64;
constexpr uint32_t kEpdSpiHz = 4000000;
constexpr uint32_t kEpdBusyTimeoutMs = 45000;
constexpr uint32_t kEpdStageTimeoutMs = 50000;
constexpr uint32_t kEpdPostResetReadyMs = 5000;

// GPIO25 enables both the SHT30 low-side ground switch and the battery
// divider.  It is not an ESP32 power-hold signal and must normally stay LOW.
constexpr uint8_t kPeripheralEnable = 25;
constexpr uint8_t kI2cSda = 21;
constexpr uint8_t kI2cScl = 22;
constexpr uint8_t kSht30Address = 0x44;
constexpr uint8_t kBatteryAdc = 36;
constexpr float kBatteryDividerRatio = 2.0f;

// Final enclosure orientation: all controls are on the right side. From top to
// bottom: USB-C, power switch, EN reset, GPIO34, GPIO35, GPIO39.
// GPIO34/35/39 are input-only and have external pull-ups on the PCB.
constexpr uint8_t kButtonUpper = 34;
constexpr uint8_t kButtonMiddle = 35;
constexpr uint8_t kButtonLower = 39;
constexpr uint8_t kButtonPressedLevel = LOW;

constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kButtonLongPressMs = 900;
constexpr uint32_t kSensorPeriodMs = 60000;
constexpr uint32_t kBridgePollPeriodMs = 5000;
constexpr uint32_t kBridgeHttpTimeoutMs = 3000;
constexpr uint32_t kOrdinaryRefreshFloorMs = 60000;
constexpr uint32_t kUrgentRefreshFloorMs = 30000;
constexpr uint32_t kUiCoalesceMs = 400;

constexpr uint16_t kDeviceHttpPort = 80;
constexpr uint16_t kDefaultBridgePort = 8765;
constexpr char kProvisioningApName[] = "Epaper75-Setup";

}  // namespace board
