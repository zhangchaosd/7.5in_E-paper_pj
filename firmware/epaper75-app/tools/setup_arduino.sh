#!/usr/bin/env bash
set -euo pipefail

arduino_cli="${ARDUINO_CLI:-arduino-cli}"
if ! command -v "$arduino_cli" >/dev/null 2>&1; then
  echo "Arduino CLI not found: $arduino_cli" >&2
  echo "On macOS install it with: brew install arduino-cli" >&2
  exit 1
fi

esp32_index='https://espressif.github.io/arduino-esp32/package_esp32_index.json'
"$arduino_cli" core update-index --additional-urls "$esp32_index"
"$arduino_cli" core install 'esp32:esp32@3.3.10' --additional-urls "$esp32_index"

"$arduino_cli" lib install 'GxEPD2@1.6.9'
"$arduino_cli" lib install 'Adafruit GFX Library@1.12.6'
"$arduino_cli" lib install 'Adafruit BusIO@1.17.4'
"$arduino_cli" lib install 'ArduinoJson@7.4.3'
"$arduino_cli" lib install 'WiFiManager@2.0.17'

echo 'Pinned Arduino toolchain is installed.'
