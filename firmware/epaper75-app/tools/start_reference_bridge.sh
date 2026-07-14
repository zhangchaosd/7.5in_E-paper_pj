#!/usr/bin/env bash
set -euo pipefail

bridge_root="${1:-${EPAPER75_REFERENCE_BRIDGE_ROOT:-}}"
if [[ -z "$bridge_root" ]]; then
  echo 'Pass the external esp8266-ai checkout path or set EPAPER75_REFERENCE_BRIDGE_ROOT.' >&2
  exit 1
fi

mac_app="$bridge_root/mac-app"
if [[ ! -f "$mac_app/Package.swift" ]]; then
  echo "macOS Bridge package not found: $mac_app" >&2
  exit 1
fi

cd "$mac_app"
swift build -c release
exec .build/release/AIClockBridge
