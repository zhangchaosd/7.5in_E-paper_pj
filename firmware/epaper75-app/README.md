# Epaper75 P1 application

This is the first product-oriented firmware for the recovered ESP32 e-paper
device.  It is intentionally separate from `epaper75-bringup`, which remains
the immutable P0 hardware diagnostic and rollback reference.

## Confirmed hardware target

- ESP32-WROOM-32E, 8 MB flash, no PSRAM
- 7.5-inch 800x480 black/white/red panel using the proven
  `GxEPD2_750c_Z08` driver
- EPD: SCK 18, MOSI 23, CS 5, DC 19, RST 16, BUSY 17
- SHT30: SDA 21, SCL 22, address 0x44
- GPIO25 gates both SHT30 ground and the battery divider and is normally LOW
- Battery ADC: GPIO36, nominal 10k/10k divider (field calibration still due)
- Right-side order from top to bottom after USB-C and the power switch:
  hardware EN reset, GPIO34 (Upper), GPIO35 (Middle), GPIO39 (Lower)

## P1 behavior

- Keeps Wi-Fi credentials in the existing ESP32 Wi-Fi/NVS store and opens the
  `Epaper75-Setup` captive portal only when normal connection fails.
- Polls an external desktop Bridge at `GET /status` without changing its JSON
  contract.
- Serves a compatibility API on port 80: `/api/info`, `/api/bridge`,
  `/api/display`, `/api/refresh`, and `/api/health`.
- Renders a full-refresh, dual-column home page: Codex quota on the left and
  four-site network latency on the right. Claude is still accepted in the
  Bridge compatibility payload but is not shown.
- Codex bars show **remaining** quota (`100 - used_percent`), so a reset starts
  full and normal use drains the bar. Reset times are rendered as absolute
  `YYYY-MM-DD HH:MM` clock times in UTC+8 rather than countdowns.
- Measures DNS + TCP/443 connection time to NetEase Cloud Music, GitHub,
  Google, and YouTube. The first round is fast; afterward each site is sampled
  about once per minute. This is connection latency, not ICMP ping or full page
  load time.
- Shows a voltage-derived battery estimate as `~NN%`. The board has no fuel
  gauge IC, so the result is intentionally marked approximate until the ADC
  divider is calibrated against a multimeter.
- Uses a compact single-line header: title, battery icon/percentage, Wi-Fi
  icon, and Bridge link icon. Online icons are black; offline/critical icons
  are red. The former device/IP second line and home-page footnotes are omitted.
- Uses a flat two-column composition rather than nested cards: large remaining
  percentages and bars on the left, one continuous four-row latency table on
  the right. Lines are reserved for hierarchy and row separation.
- Keeps temperature and humidity on the System page only. Normal display
  changes are rate-limited to protect the panel.
- Runs the HTTP API on the other ESP32 core, so a roughly 21-second e-paper
  refresh does not make discovery endpoints disappear.

The screen deliberately contains no button legend. In the final right-edge
orientation, short presses are Upper (next page), Middle (refresh), and Lower
(back); the separate uppermost control is the hardware EN/reset button.

## Build

The normative build is Arduino CLI with ESP32 core 3.3.10, GxEPD2 1.6.9,
Adafruit GFX 1.12.6, Adafruit BusIO 1.17.4, ArduinoJson 7.4.3, and WiFiManager
2.0.17. Windows PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\setup_arduino.ps1
powershell -ExecutionPolicy Bypass -File .\tools\build_arduino.ps1
```

macOS/Linux:

```bash
./tools/setup_arduino.sh
./tools/build_arduino.sh
```

Transient compiler output is placed below `%TEMP%` because the Espressif
Windows assembler does not reliably emit object files under a path containing
Chinese characters.

No PlatformIO environment is supplied yet: the official
`espressif32@6.12.0` platform uses Arduino core 2.0.17 and is not equivalent to
the verified 3.3.10 toolchain.  Adding a PIO configuration is deferred until it
can pin the same core rather than silently changing runtime behavior.

## Flash safety

Only the locally built `dist/epaper75-p1-app.bin` at address `0x10000` is needed
for the current device. The provided scripts refuse to proceed unless the
caller explicitly confirms the verified factory backup:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_app0_only.ps1 `
  -Port <COM端口> -VerifiedFactoryBackup
```

```bash
./tools/flash_app0_only.sh --port /dev/cu.<device> --verified-factory-backup
```

This deliberately leaves the bootloader, partition table, NVS, OTA metadata,
factory app1, and SPIFFS untouched.

## Bridge prerequisite

The verified reference Bridge is an external project and is not vendored here.
Clone and build the pinned revision described in `../../docs/BRIDGE_PROTOCOL.md`,
then point the helper at that external checkout:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start_reference_bridge.ps1 `
  -BridgeRoot C:\path\to\esp8266-ai
powershell -ExecutionPolicy Bypass -File .\tools\stop_reference_bridge.ps1
```

```bash
./tools/start_reference_bridge.sh /path/to/esp8266-ai
```

The Bridge remains an unmodified external reference implementation for P1. A
later owned P2 implementation can add formal `device_type=epaper75` validation,
pairing authentication, and e-paper-specific menu choices.
