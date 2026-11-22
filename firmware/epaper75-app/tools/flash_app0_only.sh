#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: flash_app0_only.sh --port /dev/cu.<device> --verified-factory-backup
       [--backup-dir PATH] [--app PATH] [--baud 115200..921600]

Only app0 at 0x10000 is written. Full-flash erase/write is not supported.
USAGE
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
project_root="$(cd -- "$script_dir/.." && pwd -P)"
firmware_root="$(cd -- "$project_root/.." && pwd -P)"
port=''
baud='460800'
app_binary="$project_root/dist/epaper75-p1-app.bin"
backup_directory="${EPAPER75_BACKUP_DIR:-$firmware_root/epaper75-bringup/backups}"
confirmed='false'

while (( $# > 0 )); do
  case "$1" in
    --port) port="${2:-}"; shift 2 ;;
    --baud) baud="${2:-}"; shift 2 ;;
    --app) app_binary="${2:-}"; shift 2 ;;
    --backup-dir) backup_directory="${2:-}"; shift 2 ;;
    --verified-factory-backup) confirmed='true'; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ "$confirmed" != 'true' ]]; then
  echo 'Refusing to flash without --verified-factory-backup.' >&2
  exit 1
fi
if [[ -z "$port" || ! -e "$port" ]]; then
  echo "Serial port does not exist: ${port:-<missing>}" >&2
  exit 1
fi
if [[ ! "$baud" =~ ^[0-9]+$ ]] || (( baud < 115200 || baud > 921600 )); then
  echo "Invalid baud rate: $baud" >&2
  exit 1
fi
if [[ ! -f "$app_binary" ]]; then
  echo "Application binary not found: $app_binary" >&2
  exit 1
fi

file_size() {
  if [[ "$(uname -s)" == 'Darwin' ]]; then
    stat -f '%z' "$1"
  else
    stat -c '%s' "$1"
  fi
}

sha256_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print toupper($1)}'
  else
    sha256sum "$1" | awk '{print toupper($1)}'
  fi
}

app_size="$(file_size "$app_binary")"
if (( app_size > 0x250000 )); then
  echo 'Application is larger than the app0 partition.' >&2
  exit 1
fi

checksum_file="$project_root/dist/SHA256SUMS.txt"
app_name="$(basename "$app_binary")"
expected_app_hash="$(awk -v name="$app_name" 'toupper($2) == toupper(name) { print toupper($1); exit }' "$checksum_file")"
actual_app_hash="$(sha256_file "$app_binary")"
if [[ -z "$expected_app_hash" || "$actual_app_hash" != "$expected_app_hash" ]]; then
  echo 'Application SHA-256 does not match dist/SHA256SUMS.txt.' >&2
  exit 1
fi

backup_manifest="$backup_directory/SHA256SUMS.txt"
if [[ ! -f "$backup_manifest" ]]; then
  echo "Factory backup manifest not found: $backup_manifest" >&2
  exit 1
fi
verified_count=0
seen_backup_paths=()
while read -r expected_hash backup_name; do
  backup_name="${backup_name%$'\r'}"
  [[ "$expected_hash" =~ ^[0-9A-Fa-f]{64}$ ]] || continue
  backup_path="$backup_directory/$backup_name"
  if [[ ! -f "$backup_path" || "$(file_size "$backup_path")" -ne 8388608 ]]; then
    echo "Factory backup missing or wrong size: $backup_name" >&2
    exit 1
  fi
  canonical_backup_path="$(cd -- "$(dirname -- "$backup_path")" && pwd -P)/$(basename "$backup_path")"
  for seen_backup_path in ${seen_backup_paths[@]+"${seen_backup_paths[@]}"}; do
    if [[ "$seen_backup_path" == "$canonical_backup_path" ]]; then
      echo "Factory backup manifest contains a duplicate file entry: $backup_name" >&2
      exit 1
    fi
  done
  seen_backup_paths[${#seen_backup_paths[@]}]="$canonical_backup_path"
  if [[ "$(sha256_file "$backup_path")" != "$(printf '%s' "$expected_hash" | tr '[:lower:]' '[:upper:]')" ]]; then
    echo "Factory backup SHA-256 mismatch: $backup_name" >&2
    exit 1
  fi
  verified_count=$((verified_count + 1))
done < "$backup_manifest"
if (( verified_count < 2 )); then
  echo 'At least two verified 8 MB factory backups are required.' >&2
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

image_info="$(run_esptool image-info "$app_binary" 2>&1)"
if ! grep -Eqi 'ESP32' <<<"$image_info" ||
   ! grep -Eqi '8MB' <<<"$image_info" ||
   ! grep -Eqi '(40m|40 MHz)' <<<"$image_info" ||
   ! grep -Eqi 'DIO' <<<"$image_info"; then
  echo 'Application image is not the expected ESP32 / 8 MB / 40 MHz / DIO format.' >&2
  exit 1
fi

flash_info="$(run_esptool --chip esp32 --port "$port" flash-id 2>&1)"
if ! grep -Eqi '(8MB|64Mbit)' <<<"$flash_info"; then
  echo 'Attached device was not positively identified as ESP32 with 8 MB flash.' >&2
  exit 1
fi

printf 'Writing app0 only: %s\nSHA256: %s\nVerified factory backups: %s\n' \
  "$app_binary" "$actual_app_hash" "$verified_count"
run_esptool --chip esp32 --port "$port" --baud "$baud" write-flash \
  --flash-mode dio --flash-freq 40m --flash-size 8MB 0x10000 "$app_binary"
echo 'P1 app0 write completed.'
