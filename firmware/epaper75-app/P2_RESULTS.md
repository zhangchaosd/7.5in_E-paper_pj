# P2 Agent fleet validation

Date: 2026-07-19
Firmware: `0.4.0-p1`

This is the current sanitized validation record for the three-Agent Codex fleet
firmware. Network identifiers, serial-port identity, bearer credentials and
Agent installation identities are intentionally omitted.

## Build and image

- Canonical Arduino ESP32 core `3.3.10` build passed with the pinned library
  versions.
- App image size: `1,298,048` bytes, below the app0 limit `0x250000`.
- Generated SHA-256 manifest matched the app image.
- `esptool image-info` confirmed ESP32, 8 MB flash, DIO and 40 MHz.
- `dist/` contained only the allow-listed app binary plus its text manifest.

## Preview and flash safety

- The exact 800x480 Adafruit GFX font preview reported zero text overflow and
  zero text-box overlap.
- Both independent 8 MB factory backups passed size and SHA-256 verification.
- The pre-flash serial gate reported `panel=powered_off`, `error=none` and no
  panic/brownout evidence.
- The approved flash helper wrote only app0 at `0x10000`; no erase or write to
  bootloader, partition table, NVS, OTA data, app1 or SPIFFS was performed.

## Live smoke test

- Firmware version, existing Wi-Fi configuration and read-only device health
  API all passed after reboot.
- Three private USB-provisioned Agent slots completed schema and installation
  identity verification and reported `3/3` online.
- Account-level Weekly remaining data and an absolute reset timestamp were both
  present and valid; the preferred slot supplied the account data.
- The device API exposed only safe aggregates and omitted IP, SSID, token and
  installation identity fields.
- A final rate-limited physical full refresh completed with the current `3/3`
  fleet snapshot; the panel then returned to `powered_off` with `error=none`.
- No boot loop, panic or brownout was observed.

The GPIO button mapping was not changed in `0.4.0-p1`; the previously accepted
Upper/Middle/Lower physical mapping remains in place. No new destructive or
control actions were added to Home.
