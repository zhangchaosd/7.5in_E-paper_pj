#!/usr/bin/env bash
# Compiles and runs the host unit tests (macOS/Linux, needs a C++17 compiler).
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
app="$(cd -- "$here/../.." && pwd -P)"
libs="${ARDUINO_LIBRARIES_DIR:-$HOME/Documents/Arduino/libraries}"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
c++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -DARDUINO=10819 -DARDUINOJSON_ENABLE_ARDUINO_STRING=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DARDUINOJSON_ENABLE_PROGMEM=0 \
  -I"$app/tools/preview/shim" -I"$libs/ArduinoJson/src" -I"$app/include" \
  "$here/host_tests.cpp" "$app/src/net/WeatherClient.cpp" "$app/src/net/Base64.cpp" \
  "$app/tools/hosttest/settings_host.cpp" "$app/src/app/Schedule.cpp" \
  "$app/src/app/DisplaySignature.cpp" "$app/src/app/Models.cpp" "$app/src/cal/Calendar.cpp" \
  -o "$out/host_tests"
"$out/host_tests"
