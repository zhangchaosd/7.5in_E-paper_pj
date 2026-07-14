#include <Arduino.h>
#include <GxEPD2_3C.h>
#include <epd3c/GxEPD2_750c_Z08.h>
#include <SPI.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_system.h>

#include "../include/board_config.h"
#include "../include/sht30.h"

namespace {

class SafeGdew075Z08 : public GxEPD2_750c_Z08 {
 public:
  SafeGdew075Z08(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
      : GxEPD2_750c_Z08(cs, dc, rst, busy) {}

  void setBusyTimeoutMs(uint32_t timeoutMs) {
    _busy_timeout = timeoutMs * 1000UL;
  }
};

GxEPD2_3C<SafeGdew075Z08, board::kEpdPageHeight> display(
    SafeGdew075Z08(board::kEpdCs, board::kEpdDc, board::kEpdReset,
                   board::kEpdBusy));

struct KeyState {
  const char* name;
  uint8_t pin;
  uint8_t raw;
  uint8_t stable;
  uint32_t changedAtMs;
};

KeyState keys[] = {
    {"KEY1", board::kKey1, HIGH, HIGH, 0},
    {"KEY2", board::kKey2, HIGH, HIGH, 0},
    {"KEY3", board::kKey3, HIGH, HIGH, 0},
};

String serialLine;
bool epdArmed = false;
bool epdLocked = false;
uint32_t epdArmDeadlineMs = 0;

void latchThenOutput(uint8_t pin, uint8_t level) {
  // Arduino-ESP32 3.x rejects digitalWrite() before pinMode(). Seed the
  // hardware output latch through ESP-IDF first, then register the pin with
  // Arduino and assert the level once more.
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
}

void setPeripheralEnabled(bool enabled) {
  digitalWrite(board::kPeripheralEnable, enabled ? HIGH : LOW);
  if (enabled) {
    delay(15);
  }
}

void initializeSafePins() {
  // Write output latches before changing direction to avoid enable glitches.
  latchThenOutput(board::kPeripheralEnable, LOW);
  latchThenOutput(board::kEpdCs, HIGH);
  latchThenOutput(board::kEpdReset, HIGH);
  latchThenOutput(board::kEpdDc, HIGH);

  pinMode(board::kEpdBusy, INPUT);
  pinMode(board::kBatteryAdc, INPUT);
  pinMode(board::kKey1, INPUT);
  pinMode(board::kKey2, INPUT);
  pinMode(board::kKey3, INPUT);

  for (KeyState& key : keys) {
    key.raw = static_cast<uint8_t>(digitalRead(key.pin));
    key.stable = key.raw;
    key.changedAtMs = millis();
  }
}

void startPeripheralBus() {
  setPeripheralEnabled(true);
  Wire.begin(board::kI2cSda, board::kI2cScl, 100000);
}

void stopPeripheralBus() {
  Wire.end();
  pinMode(board::kI2cSda, INPUT);
  pinMode(board::kI2cScl, INPUT);
  setPeripheralEnabled(false);
}

uint32_t readBatteryMillivolts() {
  setPeripheralEnabled(true);
  analogReadResolution(12);
  // Arduino-ESP32 3.x creates the ADC channel on the first conversion.
  // Configure per-pin attenuation only after that channel exists.
  analogRead(board::kBatteryAdc);
  analogSetPinAttenuation(board::kBatteryAdc, ADC_11db);

  for (uint8_t i = 0; i < 8; ++i) {
    analogReadMilliVolts(board::kBatteryAdc);
    delay(2);
  }

  uint32_t sumMillivolts = 0;
  constexpr uint8_t kSamples = 32;
  for (uint8_t i = 0; i < kSamples; ++i) {
    sumMillivolts += analogReadMilliVolts(board::kBatteryAdc);
    delay(2);
  }
  setPeripheralEnabled(false);

  const float adcMillivolts = static_cast<float>(sumMillivolts) / kSamples;
  return static_cast<uint32_t>(
      adcMillivolts * board::kBatteryDividerRatio + 0.5f);
}

void printKeys() {
  for (const KeyState& key : keys) {
    Serial.printf("%s=%s ", key.name,
                  digitalRead(key.pin) == board::kKeyPressedLevel ? "PRESSED"
                                                                  : "released");
  }
  Serial.println();
}

void printSensor() {
  startPeripheralBus();
  const Sht30Reading reading = readSht30(Wire, board::kSht30Address);
  stopPeripheralBus();

  if (!reading.ok) {
    Serial.printf("SHT30: ERROR (%s)\n", sht30ErrorName(reading.error));
    return;
  }
  Serial.printf("SHT30: %.2f C, %.2f %%RH\n", reading.temperatureC,
                reading.humidityPercent);
}

void printBattery() {
  const uint32_t batteryMillivolts = readBatteryMillivolts();
  if (batteryMillivolts < 500) {
    Serial.printf("BATTERY: %lu mV (no battery or sampling path unavailable)\n",
                  static_cast<unsigned long>(batteryMillivolts));
    return;
  }
  Serial.printf("BATTERY: %lu mV (uncalibrated, divider ratio %.2f)\n",
                static_cast<unsigned long>(batteryMillivolts),
                board::kBatteryDividerRatio);
}

void printDeviceInfo() {
  Serial.println();
  Serial.println("=== ESP32 e-paper P0 hardware check ===");
  Serial.printf("Chip: %s, revision %d, cores %d\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores());
  Serial.printf("SDK: %s\n", ESP.getSdkVersion());
  Serial.printf("Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
  Serial.printf("Flash: %lu bytes\n",
                static_cast<unsigned long>(ESP.getFlashChipSize()));
  Serial.printf("PSRAM: %lu bytes (expected 0)\n",
                static_cast<unsigned long>(ESP.getPsramSize()));
  Serial.printf("Free heap: %lu bytes\n",
                static_cast<unsigned long>(ESP.getFreeHeap()));
  Serial.printf("EPD BUSY idle sample: %s\n",
                digitalRead(board::kEpdBusy) == LOW ? "LOW" : "HIGH");
  Serial.println("Screen refresh is DISABLED until 'EPD TEST' + 'CONFIRM'.");
  Serial.println("Back up the original 8MB flash before any upload.");
  Serial.println("Type HELP for commands.");
}

void drawEpdStage(uint8_t stage) {
  display.fillScreen(GxEPD_WHITE);
  if (stage == 1) {
    return;
  }

  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(3);
  display.setCursor(32, 55);
  display.print("ESP32 E-PAPER P0");
  display.setTextSize(2);
  display.setCursor(32, 90);
  display.print("GDEW075Z08-compatible test");

  display.fillRect(80, 150, 180, 180, GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(120, 250);
  display.print("BLACK");
  if (stage >= 3) {
    display.fillRect(320, 150, 180, 180, GxEPD_RED);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(375, 250);
    display.print("RED");
  }

  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(2);
  display.setCursor(32, 420);
  display.printf("Stage %u/3 - full refresh only", stage);
}

bool refreshEpdStage(uint8_t stage) {
  const uint32_t startedAtMs = millis();
  Serial.printf("EPD stage %u/3 starting...\n", stage);

  display.setFullWindow();
  display.firstPage();
  do {
    drawEpdStage(stage);
  } while (display.nextPage());

  const uint32_t elapsedMs = millis() - startedAtMs;
  Serial.printf("EPD stage %u completed in %lu ms; BUSY=%s\n", stage,
                static_cast<unsigned long>(elapsedMs),
                digitalRead(board::kEpdBusy) == LOW ? "LOW" : "HIGH");

  if (elapsedMs > board::kEpdStageTimeoutMs ||
      digitalRead(board::kEpdBusy) == LOW) {
    Serial.println("EPD stage failed safety checks; remaining stages locked.");
    return false;
  }
  return true;
}

void abortEpdTest() {
  digitalWrite(board::kEpdCs, HIGH);
  digitalWrite(board::kEpdReset, LOW);
  delay(25);
  digitalWrite(board::kEpdReset, HIGH);
  SPI.end();
  epdLocked = true;
  Serial.println("EPD test aborted and locked until reboot.");
}

void runEpdTest() {
  epdArmed = false;
  if (epdLocked) {
    Serial.println("EPD test is already locked for this boot. Reboot to retry.");
    return;
  }

  if (digitalRead(board::kEpdBusy) == LOW) {
    Serial.println("EPD BUSY is LOW before initialization; refusing to refresh.");
    abortEpdTest();
    return;
  }

  // GPIO19 is DC, not MISO. The display is write-only.
  SPI.begin(board::kEpdSck, -1, board::kEpdMosi, board::kEpdCs);
  display.epd2.selectSPI(
      SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.epd2.setBusyTimeoutMs(board::kEpdBusyTimeoutMs);
  display.init(board::kSerialBaud, true, 10, false);
  display.setRotation(0);

  for (uint8_t stage = 1; stage <= 3; ++stage) {
    if (!refreshEpdStage(stage)) {
      abortEpdTest();
      return;
    }
    if (stage < 3) {
      Serial.println("Waiting 7 seconds before the next full refresh...");
      delay(7000);
    }
  }

  display.hibernate();
  digitalWrite(board::kEpdCs, HIGH);
  SPI.end();
  epdLocked = true;
  Serial.println("EPD test finished. Further refreshes are locked until reboot.");
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  STATUS     chip, keys, SHT30 and battery snapshot");
  Serial.println("  KEYS       current external user-key levels");
  Serial.println("  SENSOR     gated SHT30 measurement with CRC");
  Serial.println("  BATTERY    gated ADC measurement (uncalibrated)");
  Serial.println("  EPD TEST   arm the one-time conservative screen test");
  Serial.println("  CONFIRM    run the test within 15 seconds of arming");
  Serial.println("  HELP       show this list");
}

void handleCommand(String command) {
  command.trim();
  command.toUpperCase();
  if (command.isEmpty()) {
    return;
  }

  if (command == "HELP") {
    printHelp();
  } else if (command == "KEYS") {
    printKeys();
  } else if (command == "SENSOR") {
    printSensor();
  } else if (command == "BATTERY") {
    printBattery();
  } else if (command == "STATUS") {
    printDeviceInfo();
    printKeys();
    printSensor();
    printBattery();
  } else if (command == "EPD TEST") {
    if (epdLocked) {
      Serial.println("EPD test is locked until reboot.");
      return;
    }
    epdArmed = true;
    epdArmDeadlineMs = millis() + 15000UL;
    Serial.println("WARNING: this performs three slow full-screen refreshes.");
    Serial.println("Type CONFIRM within 15 seconds to continue.");
  } else if (command == "CONFIRM") {
    if (!epdArmed || static_cast<int32_t>(epdArmDeadlineMs - millis()) <= 0) {
      epdArmed = false;
      Serial.println("EPD test is not armed. Type EPD TEST first.");
      return;
    }
    runEpdTest();
  } else {
    Serial.printf("Unknown command: %s\n", command.c_str());
    printHelp();
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r' || value == '\n') {
      if (!serialLine.isEmpty()) {
        handleCommand(serialLine);
        serialLine.clear();
      }
    } else if (value >= 32 && value <= 126 && serialLine.length() < 80) {
      serialLine += value;
    }
  }
}

void pollKeys() {
  const uint32_t now = millis();
  for (KeyState& key : keys) {
    const uint8_t raw = static_cast<uint8_t>(digitalRead(key.pin));
    if (raw != key.raw) {
      key.raw = raw;
      key.changedAtMs = now;
    }
    if (key.stable != key.raw && now - key.changedAtMs >= 25) {
      key.stable = key.raw;
      Serial.printf("%s %s\n", key.name,
                    key.stable == board::kKeyPressedLevel ? "PRESSED"
                                                          : "released");
    }
  }
}

}  // namespace

void setup() {
  initializeSafePins();
  Serial.begin(board::kSerialBaud);
  delay(500);
  printDeviceInfo();
}

void loop() {
  pollSerial();
  pollKeys();
  if (epdArmed && static_cast<int32_t>(millis() - epdArmDeadlineMs) >= 0) {
    epdArmed = false;
    Serial.println("EPD confirmation window expired.");
  }
  delay(5);
}
