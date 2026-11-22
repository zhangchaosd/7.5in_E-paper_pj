#!/usr/bin/env bash
# Writes QWeather credentials and the forecast location to the device over
# USB serial without echoing or logging them.
#
#   tools/provision_qweather.sh --port /dev/cu.usbserial-XXXX \
#       [--key ~/qweather-ed25519-private.pem] [--lat 39.92 --lon 116.41] [--reset]
#
# The device is normally in deep sleep. Press EN (or Upper/Middle) right before
# running, or pass --reset to have esptool hard-reset it into the application
# (read-only chip query; its output is discarded).
#
# The API Host, developer ID (iss), project ID (sub) and credential ID (kid)
# are read from QW_API_HOST / QW_DEVELOPER_ID / QW_PROJECT_ID /
# QW_CREDENTIAL_ID or prompted for. Opening the port resets the board; the
# script waits for it to finish booting, sends the commands, and prints only
# the device's redacted replies.
set -euo pipefail

port=''
key="$HOME/qweather-ed25519-private.pem"
lat=''
lon=''
reset='false'
while (( $# > 0 )); do
  case "$1" in
    --port) port="${2:-}"; shift 2 ;;
    --key) key="${2:-}"; shift 2 ;;
    --lat) lat="${2:-}"; shift 2 ;;
    --lon) lon="${2:-}"; shift 2 ;;
    --reset) reset='true'; shift ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ -n "$port" && -e "$port" ]] || { echo "Serial port not found: ${port:-<missing>}" >&2; exit 1; }
[[ -f "$key" ]] || { echo "Private key not found: $key" >&2; exit 1; }
if [[ -n "$lat" || -n "$lon" ]]; then
  [[ "$lat" =~ ^-?[0-9]{1,2}(\.[0-9]+)?$ && "$lon" =~ ^-?[0-9]{1,3}(\.[0-9]+)?$ ]] ||
    { echo 'Invalid --lat/--lon' >&2; exit 1; }
fi

ask() {  # var prompt
  local value="${!1:-}"
  if [[ -z "$value" ]]; then read -r -p "$2: " value; fi
  [[ "$value" =~ ^[A-Za-z0-9.-]+$ ]] || { echo "Invalid $2" >&2; exit 1; }
  printf '%s' "$value"
}
host="$(ask QW_API_HOST 'API Host (e.g. abcxyz.qweatherapi.com)')"
developer="$(ask QW_DEVELOPER_ID 'Developer ID (控制台-设置, starts with Q)')"
project="$(ask QW_PROJECT_ID 'Project ID')"
credential="$(ask QW_CREDENTIAL_ID 'Credential ID')"
key_body="$(grep -v -- '-----' "$key" | tr -d '\r\n')"
[[ "$key_body" =~ ^[A-Za-z0-9+/=]{40,80}$ ]] || { echo 'Unexpected private key format' >&2; exit 1; }

if [[ "$reset" == 'true' ]]; then
  esptool_bin="${ESPTOOL:-$(command -v esptool || true)}"
  if [[ -z "$esptool_bin" ]]; then
    esptool_bin="$(find "$HOME/Library/Arduino15/packages/esp32/tools/esptool_py" -type f -name esptool 2>/dev/null | sort | tail -n 1 || true)"
  fi
  [[ -n "$esptool_bin" ]] || { echo 'esptool not found for --reset' >&2; exit 1; }
  "$esptool_bin" --chip esp32 --port "$port" --after hard-reset read-mac >/dev/null 2>&1 ||
    { echo 'Reset via esptool failed' >&2; exit 1; }
fi

exec 3<>"$port"
stty -f "$port" 115200 raw -echo 2>/dev/null || stty -F "$port" 115200 raw -echo
cleanup() { exec 3>&- 3<&-; unset key_body; }
trap cleanup EXIT

wait_for() {  # pattern timeout-seconds
  local deadline=$((SECONDS + $2)) line
  while (( SECONDS < deadline )); do
    if IFS= read -r -t 1 -u 3 line; then
      line="${line%$'\r'}"
      case "$line" in
        *"$1"*) return 0 ;;
      esac
    fi
  done
  return 1
}
show_replies() {  # seconds
  local deadline=$((SECONDS + $1)) line
  while (( SECONDS < deadline )); do
    if IFS= read -r -t 1 -u 3 line; then
      line="${line%$'\r'}"
      case "$line" in
        "[qweather]"*|"[location]"*|"[weather]"*|"[clock]"*|"[wifi]"*|"[display]"*) echo "  $line" ;;
        *"panel="*) echo "  ${line}" | sed -E 's/next_wake_min=[0-9]+//' ;;
      esac
    fi
  done
}

echo 'Waiting for the device to boot (it may refresh the panel first)...'
wait_for '=== Epaper75 ===' 120 || { echo 'Device did not report ready; is 0.6.x flashed?' >&2; exit 1; }
sleep 1
printf 'QWEATHER %s %s %s %s %s\n' "$host" "$developer" "$project" "$credential" "$key_body" >&3
show_replies 3
if [[ -n "$lat" ]]; then
  printf 'LOCATION %s %s\n' "$lat" "$lon" >&3
  show_replies 3
fi
echo 'Fetching weather and redrawing (about a minute)...'
printf 'FETCH\n' >&3
show_replies 60
printf 'INFO\n' >&3
show_replies 3
