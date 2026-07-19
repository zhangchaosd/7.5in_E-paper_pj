# Epaper75 0.5 application

This is the product firmware for the recovered ESP32 e-paper device. The
current flashed version is `0.5.0-p1`. It is intentionally separate from
`epaper75-bringup`, which remains the P0 hardware diagnostic and rollback
reference.

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

## Current behavior

- Keeps Wi-Fi credentials in the existing ESP32 Wi-Fi/NVS store and opens the
  `Epaper75-Setup` captive portal only when normal connection fails.
- Polls three Codex Monitor Agent `0.4.x` installations with a shared Bearer
  token and the schema `1.1` contract. One Agent slot is polled every five
  seconds, so each installation is normally visited about every 15 seconds.
- Serves a privacy-bounded, read-only API on port 80: `/api/info` and
  `/api/health`. There are no HTTP write routes or network endpoints for Agent
  credentials.
- Renders a dual-column Home page: shared Codex account Weekly
  data on the left and three independent Agent activity rows on the right.
- Agent slot 0 is the fixed preferred account source. Because all three
  installations share one account, another identity-verified Agent may provide
  fallback Weekly data.
- The Weekly bar shows **remaining** quota (`100 - used_percent`), so a reset
  starts full and normal use drains the bar. Reset time is an absolute
  `YYYY-MM-DD HH:MM` clock time in UTC+8, not a countdown.
- Each device row distinguishes running, approval, input, idle, error, stale,
  offline, authentication and protocol states. Unknown is not treated as idle.
- Shows a voltage-derived battery estimate as `~NN%`. The board has no fuel
  gauge IC, so the result is intentionally marked approximate until the ADC
  divider is calibrated against a multimeter. Battery remains on System and is
  not shown in the Home header.
- Uses a compact single-line header with title, Wi-Fi icon, and live `N/3`
  Agent count only. The retired Bridge icon, battery, device/IP second line and
  Home footnotes are omitted.
- Does not start the legacy four-site `NetworkProbe`; Home and the device API
  no longer provide network-latency data.
- Keeps temperature and humidity on the System page only. Normal display
  changes are rate-limited to protect the panel.
- Uses one merged partial window for ordinary visible changes. First boot,
  page changes, explicit cleanup, dirty windows covering at least 72% of the
  screen, and residue maintenance after 24 partial updates or 6 hours use a
  full refresh. The Z08 panel has no fast-partial waveform; a partial window
  still takes roughly the same waveform time but avoids flashing the rest of
  the screen.
- Runs the HTTP API and Agent client on Core 0, so a long physical e-paper
  refresh does not make discovery endpoints or Agent polling disappear.

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

## Agent prerequisite and configuration

Run a compatible Codex Monitor Agent on each of the three desktop devices. The
firmware targets CMA `0.4.x` schema `1.1` and authenticates every JSON request
with one shared Bearer token. The Agent executable and local contract copies
are not vendored into this repository.

Provision all three endpoints and the token over a locally controlled USB
serial session at 115200 baud:

```text
AGENTS <endpoint0> <endpoint1> <endpoint2> <shared-token>
```

Slot 0 is always the primary account source. The firmware stores the complete
setting in its private NVS blob and only prints a sanitized success or failure
message; it does not echo arguments. Full field rules, polling behavior and
failure semantics are in
[`../../docs/AGENT_PROTOCOL.md`](../../docs/AGENT_PROTOCOL.md).

Never put real endpoints, tokens, the raw serial command or installation IDs in
source, Git, shell history, terminal logs, screenshots or test output. The
firmware deliberately has no `/api/agents` or Wi-Fi provisioning field for
these secrets. The retired Bridge workflow is documented only as a historical
migration pointer in `../../docs/BRIDGE_PROTOCOL.md` and is not required by
`0.5.x`.
