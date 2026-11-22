#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
sketch_dir="$(cd -- "$script_dir/.." && pwd -P)"
include_dir="$sketch_dir/include"
dist_dir="$sketch_dir/dist"
arduino_cli="${ARDUINO_CLI:-arduino-cli}"
libraries_dir="${ARDUINO_LIBRARIES_DIR:-$HOME/Documents/Arduino/libraries}"
build_dir="${BUILD_DIR:-${TMPDIR:-/tmp}/epaper75-app-build}"
fqbn='esp32:esp32:esp32:FlashSize=8M,PartitionScheme=custom,PSRAM=disabled,FlashMode=dio,FlashFreq=40,EraseFlash=none,DebugLevel=info'

if ! command -v "$arduino_cli" >/dev/null 2>&1; then
  echo "Arduino CLI not found: $arduino_cli" >&2
  exit 1
fi
if [[ ! -d "$libraries_dir" ]]; then
  echo "Arduino libraries directory not found: $libraries_dir" >&2
  exit 1
fi
if ! "$arduino_cli" core list | grep -Eq '^esp32:esp32[[:space:]]+3\.3\.10[[:space:]]'; then
  echo 'The normative build requires esp32:esp32 core 3.3.10.' >&2
  exit 1
fi

while IFS='|' read -r directory expected; do
  properties="$libraries_dir/$directory/library.properties"
  if [[ ! -f "$properties" ]]; then
    echo "Required Arduino library is missing: $directory" >&2
    exit 1
  fi
  actual="$(awk -F= '$1 == "version" { print $2; exit }' "$properties" | tr -d '\r')"
  if [[ "$actual" != "$expected" ]]; then
    echo "$directory version is ${actual:-unknown}; required $expected." >&2
    exit 1
  fi
done <<'VERSIONS'
GxEPD2|1.6.9
Adafruit_GFX_Library|1.12.6
Adafruit_BusIO|1.17.4
ArduinoJson|7.4.3
WiFiManager|2.0.17
VERSIONS

mkdir -p "$build_dir"
"$arduino_cli" compile \
  --fqbn "$fqbn" \
  --build-path "$build_dir" \
  --build-property "compiler.cpp.extra_flags=\"-I$include_dir\"" \
  --libraries "$libraries_dir" \
  "$sketch_dir"

source_app="$build_dir/epaper75-app.ino.bin"
if [[ ! -f "$source_app" ]]; then
  echo "Expected app artifact missing: $source_app" >&2
  exit 1
fi

mkdir -p "$dist_dir"
app_output="$dist_dir/epaper75-p1-app.bin"
cp "$source_app" "$app_output"
find "$dist_dir" -maxdepth 1 -type f -name '*.bin' ! -name 'epaper75-p1-app.bin' -delete

if [[ "$(uname -s)" == 'Darwin' ]]; then
  size="$(stat -f '%z' "$app_output")"
else
  size="$(stat -c '%s' "$app_output")"
fi
if (( size > 0x250000 )); then
  echo "P1 application exceeds the app0 limit: $size bytes" >&2
  exit 1
fi

if command -v shasum >/dev/null 2>&1; then
  digest="$(shasum -a 256 "$app_output" | awk '{print toupper($1)}')"
else
  digest="$(sha256sum "$app_output" | awk '{print toupper($1)}')"
fi
printf '%s  %s\n' "$digest" "$(basename "$app_output")" > "$dist_dir/SHA256SUMS.txt"
printf 'app=%s\nbytes=%s\nsha256=%s\n' "$app_output" "$size" "$digest"
