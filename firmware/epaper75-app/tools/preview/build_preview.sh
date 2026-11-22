#!/usr/bin/env bash
# Builds the host renderer preview and writes PNGs into the given directory.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
app="$(cd -- "$here/../.." && pwd -P)"
out="${1:?usage: build_preview.sh <output-dir>}"
gfx="${ARDUINO_LIBRARIES_DIR:-$HOME/Documents/Arduino/libraries}/Adafruit_GFX_Library"
mkdir -p "$out"
c++ -std=c++17 -O1 -w -DARDUINO=10819 -I"$here/shim" -I"$gfx" -I"$app/include" \
  "$here/preview_main.cpp" "$gfx/Adafruit_GFX.cpp" \
  "$app/src/ui/Renderer.cpp" "$app/src/ui/Fonts.cpp" "$app/src/ui/Icons.cpp" \
  "$app/src/ui/fonts/"*.cpp "$app/src/cal/Calendar.cpp" "$app/src/weather/WeatherText.cpp" \
  -o "$out/preview"
"$out/preview" "$out"
for f in "$out"/*.bmp; do sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null && rm "$f"; done
ls "$out"
