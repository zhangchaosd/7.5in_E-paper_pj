#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: backup_flash.sh --port /dev/cu.<device> [--backup-dir PATH]
       [--baud 115200..921600]

Reads the complete 8 MB flash without writing or erasing the device.
The output may contain private NVS/Wi-Fi data and must not be published.
USAGE
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_root="$(cd -- "$script_dir/.." && pwd -P)"
port=''
baud='460800'
backup_directory="${EPAPER75_BACKUP_DIR:-$project_root/backups}"

while (( $# > 0 )); do
  case "$1" in
    --port) port="${2:-}"; shift 2 ;;
    --baud) baud="${2:-}"; shift 2 ;;
    --backup-dir) backup_directory="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$port" || ! -e "$port" ]]; then
  echo "Serial port does not exist: ${port:-<missing>}" >&2
  exit 1
fi
if [[ ! "$baud" =~ ^[0-9]+$ ]] || (( baud < 115200 || baud > 921600 )); then
  echo "Invalid baud rate: $baud" >&2
  exit 1
fi

esptool_mode='binary'
esptool_bin="${ESPTOOL:-}"
if [[ -z "$esptool_bin" ]]; then
  if command -v esptool >/dev/null 2>&1; then
    esptool_bin="$(command -v esptool)"
  elif command -v esptool.py >/dev/null 2>&1; then
    esptool_bin="$(command -v esptool.py)"
  elif python3 -m esptool version >/dev/null 2>&1; then
    esptool_mode='python'
  else
    tools_root="$HOME/Library/Arduino15/packages/esp32/tools/esptool_py"
    if [[ -d "$tools_root" ]]; then
      esptool_bin="$(find "$tools_root" -type f \( -name esptool -o -name esptool.py \) 2>/dev/null | sort | tail -n 1 || true)"
    fi
  fi
fi
if [[ "$esptool_mode" == 'binary' && ( -z "$esptool_bin" || ! -f "$esptool_bin" ) ]]; then
  echo 'Unable to locate esptool. Set ESPTOOL or install python3 -m esptool.' >&2
  exit 1
fi

run_esptool() {
  if [[ "$esptool_mode" == 'python' ]]; then
    python3 -m esptool "$@"
  else
    "$esptool_bin" "$@"
  fi
}

flash_info="$(run_esptool --chip esp32 --port "$port" flash-id 2>&1)"
if ! grep -Eqi '(8MB|64Mbit)' <<<"$flash_info"; then
  echo 'Attached device was not positively identified as ESP32 with 8 MB flash.' >&2
  exit 1
fi

mkdir -p "$backup_directory"
timestamp="$(date '+%Y%m%d-%H%M%S')"
port_name="${port##*/}"
output="$backup_directory/factory-original-$timestamp-$port_name-8mb.bin"
if [[ -e "$output" ]]; then
  echo "Refusing to overwrite existing backup: $output" >&2
  exit 1
fi

run_esptool --chip esp32 --port "$port" --baud "$baud" read-flash 0x000000 0x800000 "$output"
if [[ "$(uname -s)" == 'Darwin' ]]; then
  size="$(stat -f '%z' "$output")"
else
  size="$(stat -c '%s' "$output")"
fi
if [[ "$size" -ne 8388608 ]]; then
  echo "Backup has invalid size: $size bytes" >&2
  exit 1
fi

if command -v shasum >/dev/null 2>&1; then
  digest="$(shasum -a 256 "$output" | awk '{print toupper($1)}')"
else
  digest="$(sha256sum "$output" | awk '{print toupper($1)}')"
fi
printf '%s  %s\n' "$digest" "$(basename "$output")" >> "$backup_directory/SHA256SUMS.txt"
printf 'backup=%s\nbytes=%s\nsha256=%s\n' "$output" "$size" "$digest"
