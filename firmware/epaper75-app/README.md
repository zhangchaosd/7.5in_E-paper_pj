# Epaper75 0.6 application

Product firmware for the recovered ESP32 e-paper device: a month calendar
with lunar dates, solar terms and statutory holidays, plus current weather,
air quality, alerts and a six-day forecast from QWeather. The device spends
almost all of its time in deep sleep. `epaper75-bringup` remains the P0
hardware diagnostic and rollback reference.

Current flashed version: `0.6.0-p1`; flash and smoke-test record in
`P5_RESULTS.md`. `P4_RESULTS.md` covers the earlier `0.5.1-p1` Codex dashboard.

## Confirmed hardware target

- ESP32-WROOM-32E, 8 MB flash, no PSRAM
- 7.5-inch 800x480 black/white/red panel using the proven
  `GxEPD2_750c_Z08` driver, full refresh only
- EPD: SCK 18, MOSI 23, CS 5, DC 19, RST 16, BUSY 17
- SHT30: SDA 21, SCL 22, address 0x44
- GPIO25 gates both SHT30 ground and the battery divider and is held LOW
  through deep sleep
- Battery ADC: GPIO36, nominal 10k/10k divider (field calibration still due)
- Right-side order: USB-C, power switch, EN reset, GPIO34 (Upper),
  GPIO35 (Middle), GPIO39 (Lower)

## Behavior

- Wakes on a fixed Beijing-time schedule (00:02, 06:30, 09:00, 12:00, 15:00,
  18:00, 21:00 full updates; hourly alert checks 07–22 h) and redraws only when
  the visible page changed. Details: [`../../docs/POWER.md`](../../docs/POWER.md).
- Upper and Middle wake the device. Short: Upper toggles Home/System, Middle
  fetches and redraws, Lower returns Home. Long: Upper fetches, Middle opens
  System, Lower returns Home. The device stays awake 90 s after the last
  button or serial command.
- Wi-Fi uses the credentials already stored by the ESP32 driver. Without any
  stored network the `Epaper75-Setup` portal opens (10 minute timeout); the
  `WIFI SETUP` serial command reopens it.
- QWeather access uses the account API Host and an on-device Ed25519 JWT;
  details: [`../../docs/WEATHER_PROTOCOL.md`](../../docs/WEATHER_PROTOCOL.md).
- Battery percentage is voltage-derived and always shown as `~NN%`; a red
  battery mark appears in the Home header only at or below ~15%.

## Build, test and preview

```bash
./tools/setup_arduino.sh
./tools/build_arduino.sh                 # app0 image into dist/
./tools/hosttest/run_host_tests.sh       # parsers, schedule, signatures
./tools/preview/build_preview.sh /tmp/epaper75-preview   # 800x480 PNGs
```

Regenerating data (macOS only, outputs are committed):

```bash
swift tools/gen/gen_calendar.swift > include/cal/CalendarData.h
tools/gen/gen_fonts.sh   # needs Noto Sans SC OTFs in ~/Library/Caches/epaper75-fonts
```

The normative toolchain is unchanged: Arduino CLI with ESP32 core 3.3.10,
GxEPD2 1.6.9, Adafruit GFX 1.12.6, Adafruit BusIO 1.17.4, ArduinoJson 7.4.3,
WiFiManager 2.0.17. JWT signing (libsodium), gzip (ROM tinfl) and the TLS root
bundle all come from the core; no new libraries were added.

## Flash safety

Only `dist/epaper75-p1-app.bin` at `0x10000` is ever written, through
`tools/flash_app0_only.sh` / `.ps1`, after two verified factory backups exist.
Read [`../../docs/FLASH_SAFETY.md`](../../docs/FLASH_SAFETY.md) first.

## Provisioning

1. Generate a dedicated key pair: `swift tools/qweather_keygen.swift`, upload
   the printed public key in the QWeather console, note the credential ID.
2. Write credentials and location over USB serial:

   ```bash
   ./tools/provision_qweather.sh --port /dev/cu.<device> --lat <lat> --lon <lon> --reset
   ```

   It prompts for the API Host, developer ID, project ID and credential ID,
   reads the private key from `~/qweather-ed25519-private.pem`, and prints only
   the device's redacted replies.

Never commit or paste the private key, API Host, IDs or coordinates.
