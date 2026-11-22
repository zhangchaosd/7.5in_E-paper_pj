# AGENTS.md

This file is the fast-start contract for AI agents and developers working in this repository. Read it before changing code or touching hardware.

## Goal and canonical project

The product target is a recovered 7.5-inch tri-color e-paper device showing a
month calendar (lunar dates, solar terms, statutory holidays) and QWeather
current conditions, air quality, alerts and a six-day forecast. The canonical
firmware is:

```text
firmware/epaper75-app
```

Current flashed version: `0.6.0-p1` (calendar + weather, deep sleep); its
flash and smoke-test record is `firmware/epaper75-app/P5_RESULTS.md`. Before
it, app0 held an unrecorded `0.6.0-p1` build whose source is not in this
repository (see P5). `P4_RESULTS.md` records `0.5.1-p1` (Codex fleet dashboard). `P3_RESULTS.md` records the withdrawn
`0.5.0-p1` partial-window experiment and must not be treated as an approved
strategy. `P2_RESULTS.md` and `P1_RESULTS.md` preserve older history.

`firmware/epaper75-bringup` is the P0 diagnostic/reference project. Keep it available for hardware regression work, but do not merge P0 display routines into the product firmware without review.

Start with:

- `README.md`
- `docs/HARDWARE.md`
- `docs/DEVELOPMENT.md`
- `docs/FLASH_SAFETY.md`
- `docs/POWER.md`
- `docs/WEATHER_PROTOCOL.md`
- `firmware/epaper75-app/ARCHITECTURE.md`
- `firmware/epaper75-app/P5_RESULTS.md`
- `firmware/epaper75-app/P4_RESULTS.md`
- `firmware/epaper75-app/P3_RESULTS.md`

## Non-negotiable hardware and flash safety

1. The target is an ESP32-WROOM-32E with 8 MB flash and no PSRAM.
2. The panel is 800x480 black/white/red. Use `GxEPD2_750c_Z08` and the existing project-specific bounded BUSY full-refresh path. Partial-window refresh is prohibited: physical testing caused persistent half-screen shadowing.
3. Only the app0 partition at offset `0x10000` may be written during normal development.
4. Never erase the full flash. Never overwrite the bootloader, partition table, NVS, OTA data, app1, or SPIFFS.
5. Never use an IDE/PlatformIO generic upload action on this device; those flows can write more than app0.
6. Before every physical flash, confirm serial `INFO` reports `panel=powered_off` and no panel error.
7. Flashing requires two independently verified local 8 MB factory backups in `firmware/epaper75-bringup/backups/`. They are private, ignored by Git, and must never be uploaded or deleted.
8. Use only `tools/flash_app0_only.ps1` on Windows or `tools/flash_app0_only.sh` on macOS/Linux. Read `docs/FLASH_SAFETY.md` first.
9. Do not expose device IP, MAC, SSID, Agent host, serial-port identity, auth files, Bearer tokens, Agent installation IDs, or full serial `INFO` output in commits, issues, logs, or responses.

If a proposed change weakens any of these constraints, stop and ask the owner.

## Confirmed hardware map

| Function | GPIO / value |
|---|---:|
| EPD SCK | 18 |
| EPD MOSI | 23 |
| EPD CS | 5 |
| EPD DC | 19 |
| EPD RST | 16 |
| EPD BUSY | 17 |
| SHT30 SDA / SCL | 21 / 22 |
| SHT30 address | `0x44` |
| Peripheral gate | 25 |
| Battery ADC | 36 |
| Upper / Middle / Lower buttons | 34 / 35 / 39 |

GPIO25 enables both the SHT30 low-side ground path and the battery divider. It must normally remain LOW and be raised only for a bounded sensor/battery sample; it is held LOW with `gpio_hold_en` through deep sleep. It is not a system power-hold pin.

After sampling, return the I2C pins to input as the existing board code does, preventing back-power through SDA/SCL. GPIO34/35/39 have external 10k pull-ups and must not use `INPUT_PULLUP`. GPIO0 belongs to CH340 auto-download control. The unpopulated 8025 footprint is not an available RTC, and charger status LEDs are not connected to readable GPIOs.

Final physical order on the right edge, top to bottom: USB-C, power switch, EN/reset, Upper (GPIO34), Middle (GPIO35), Lower (GPIO39). GPIO34/35/39 are input-only and use PCB pull-ups; pressed is LOW.

## Product behavior that must be preserved

- Home: header (place name from GeoAPI, "HH:MM 更新" or a red stale /
  alert banner, red low-battery mark only at <= ~15%, Wi-Fi state); today panel
  (date, weekday, lunar date, festival/solar-term/休/班 chips, ISO week);
  current weather (icon, temperature, condition, high/low, feels-like); facts
  grid (wind, AQI, humidity, sunrise, sunset, today's precipitation chance); Monday-first
  month calendar with lunar labels and 休/班 marks; six-day forecast strip.
- System page: battery `~NN%` and voltage, next wake, wake count, last fetch
  result/time, alerts, Wi-Fi, firmware, refresh mode. Indoor temperature and
  humidity are intentionally not shown and the SHT30 is not sampled.
- Battery percentage is voltage-derived and must remain visibly approximate (`~`).
- Home has no button legend or help copy. By owner decision no QWeather
  wording is shown on screen (check the QWeather terms before publishing).
- Red is reserved for the title accent, today, weekend headers, festivals and
  solar terms, 休 marks, the sun, severe weather, poor AQI, alerts, stale or
  failed data and low battery.
- Short buttons: Upper Home/System, Middle fetch + redraw, Lower Home.
  Long buttons: Upper fetch, Middle System, Lower Home. Only Upper and Middle
  can wake from deep sleep (ESP32 ext0/ext1).
- Power: deep sleep between the fixed wake table in `docs/POWER.md`; redraw
  only when the visible-content signature changed; radio on only during a
  fetch. Use only the proven full-screen waveform. Never re-enable partial or
  fast refresh from library capability flags alone; the physical `0.5.0-p1`
  trial produced unacceptable half-screen shadowing after repeated updates.

## Architecture map

```text
board/    GPIO startup, buttons, SHT30, battery sampling
cal/      offline lunar calendar, solar terms, festivals, holiday table
weather/  condition-code text/icons, wind and AQI wording
net/      settings (NVS), Wi-Fi + NTP, QWeather client (JWT, TLS, gzip)
app/      display model, visible signature, wake schedule
ui/       fonts, icons, renderer and the only physical panel-refresh entry point
```

Everything runs sequentially on the Arduino loop task and every boot ends in
deep sleep. There is no device HTTP server and no background network task.

All display traffic must continue through `ui::EpdController`. The bounded recovery, RTC journal, one-retry rule, power-off step, and `FaultLocked` behavior are safety mechanisms, not optional abstractions.

## Reproducible build

Pinned toolchain:

- Arduino ESP32 core `3.3.10`
- GxEPD2 `1.6.9`
- Adafruit GFX `1.12.6`
- Adafruit BusIO `1.17.4`
- ArduinoJson `7.4.3`
- WiFiManager `2.0.17`

macOS/Linux:

```bash
cd firmware/epaper75-app
./tools/setup_arduino.sh
./tools/build_arduino.sh
```

Windows PowerShell:

```powershell
cd firmware\epaper75-app
.\tools\setup_arduino.ps1
.\tools\build_arduino.ps1
```

The build must remain below the app0 limit `0x250000` bytes and emit only the allow-listed app image into `dist/`. The expected FQBN properties are 8 MB flash, custom partition scheme, no PSRAM, DIO, 40 MHz, and no erase.

PlatformIO is not normative because its currently documented platform/core combination does not reproduce Arduino core 3.3.10. Do not add or use a PlatformIO upload path unless it pins an equivalent core and preserves the app0-only safety boundary.

## Validation before committing

For documentation-only changes:

- inspect `git diff --check`;
- verify Markdown links and commands;
- run a secret/large-file scan.

For source or build-script changes:

- run the canonical full build;
- verify the generated app size and SHA-256 manifest;
- inspect `esptool image-info` for ESP32, 8 MB, DIO, 40 MHz;
- review that no command writes outside `0x10000`;
- check static RAM/flash usage for regressions.

Also run `tools/hosttest/run_host_tests.sh` and regenerate the renderer preview
with `tools/preview/build_preview.sh` for any change in `app/`, `cal/`, `net/`
parsers, `weather/` or `ui/`.

For hardware-visible changes, also perform a proportionate smoke test: serial boot with no panic, Wi-Fi and QWeather fetch result, button mapping and both wake buttons, one physical full refresh, a timer wake, and final `panel=powered_off`. Partial-window testing requires a separate owner-approved hardware experiment and must not enter product firmware. Never publish raw network identifiers from that test.

## Weather service boundary

The firmware calls the account-specific QWeather API Host over verified HTTPS
with an on-device Ed25519 JWT (`kid` = credential ID, `iss` = developer ID,
`sub` = project ID, 15-minute lifetime after NTP sync). The consumed endpoints
and fields are documented in `docs/WEATHER_PROTOCOL.md`. Do not switch to the
retired public hosts or to API-KEY authentication.

The API Host, the three IDs, the Ed25519 private key and the coordinates are
persisted in the project-owned NVS namespace only through the physical USB
serial commands `QWEATHER ...` and `LOCATION ...` (helper:
`tools/provision_qweather.sh`). They must stay in `SettingsStore`/
`WeatherClient` and never enter `UiModel`, renderer output, serial output, Git,
screenshots or test logs. The raw `QWEATHER` line is secret. There is no
network configuration endpoint; never add one or put these values into the
Wi-Fi portal without owner review.

Weather condition text is chosen by the firmware from `condition.code` so the
generated font covers it; do not render arbitrary API text in fonts that lack
GB2312 coverage.

The retired Codex Monitor Agent client (`0.4.x`–`0.5.x`) and its contract
(`docs/AGENT_PROTOCOL.md`) are historical only. The old `agent_fleet` NVS blob
may still exist on a device flashed with 0.5.x; 0.6.x ignores it.

## Repository hygiene

- Keep reverse-engineering dumps, downloaded vendor packages, SDKs, build caches, full flash images, and reference-project checkouts outside Git.
- Font source files (Noto Sans SC OTF) stay outside Git; only the generated glyph tables in `src/ui/fonts/` are committed. Rerun `tools/gen/gen_fonts.sh` after adding Chinese UI strings.
- Update the statutory holiday table in `src/cal/Calendar.cpp` every November.
- Do not commit generated bootloader, partition, `boot_app0`, ELF, map, or object files.
- Do not track firmware binaries in normal Git history. The project is GPL-3.0-only; publish a binary only through a reviewed release that satisfies `THIRD_PARTY_NOTICES.md` and makes the corresponding source/license materials available. Local builds remain under ignored `dist/`.
- Preserve unrelated user work and local backups.
- Use small, reviewable commits and update documentation when pin maps, protocol fields, safety rules, or tool versions change.
- Prefer portable paths and POSIX shell for macOS support; keep PowerShell equivalents for Windows device work.

For renderer changes, make and inspect an 800x480 preview before building or flashing. For `EpdController`, BUSY handling, reset, power-off, or journal changes, raise the review and physical-test level; never validate those changes by UI inspection alone.

## Current open hardware work

- Flash `0.6.0-p1`, provision QWeather, and confirm real API field shapes
  against the parsers (the v1 schemas were implemented from documentation).
- Verify timer wake, Upper/Middle button wake, GPIO25 held LOW in sleep, and
  that a held button does not cause a wake loop.
- Measure deep-sleep current and per-wake energy; update `docs/POWER.md`.
- Visually confirm full refresh leaves no half-screen shadow.
- Test EN/reset during an active refresh and confirm bounded recovery without a refresh loop.
- Calibrate battery voltage/percentage against a multimeter; validate the
  3.45 V low-battery cutoff.
