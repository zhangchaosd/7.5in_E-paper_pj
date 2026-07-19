# AGENTS.md

This file is the fast-start contract for AI agents and developers working in this repository. Read it before changing code or touching hardware.

## Goal and canonical project

The product target is a recovered 7.5-inch tri-color e-paper device. The canonical firmware is:

```text
firmware/epaper75-app
```

Current verified version: `0.4.0-p1`. Its sanitized device-verification record
is `firmware/epaper75-app/P2_RESULTS.md`. `P1_RESULTS.md` is historical for
`0.3.0-p1`; do not treat it as the current Home page or Agent protocol.

`firmware/epaper75-bringup` is the P0 diagnostic/reference project. Keep it available for hardware regression work, but do not merge P0 display routines into the product firmware without review.

Start with:

- `README.md`
- `docs/HARDWARE.md`
- `docs/DEVELOPMENT.md`
- `docs/FLASH_SAFETY.md`
- `docs/AGENT_PROTOCOL.md`
- `firmware/epaper75-app/ARCHITECTURE.md`
- `firmware/epaper75-app/P2_RESULTS.md`
- `firmware/epaper75-app/P1_RESULTS.md`

## Non-negotiable hardware and flash safety

1. The target is an ESP32-WROOM-32E with 8 MB flash and no PSRAM.
2. The panel is 800x480 black/white/red. Use `GxEPD2_750c_Z08` and the existing project-specific bounded BUSY/full-refresh path.
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

GPIO25 enables both the SHT30 low-side ground path and the battery divider. It must normally remain LOW and be raised only for a bounded sensor/battery sample. It is not a system power-hold pin.

After sampling, return the I2C pins to input as the existing board code does, preventing back-power through SDA/SCL. GPIO34/35/39 have external 10k pull-ups and must not use `INPUT_PULLUP`. GPIO0 belongs to CH340 auto-download control. The unpopulated 8025 footprint is not an available RTC, and charger status LEDs are not connected to readable GPIOs.

Final physical order on the right edge, top to bottom: USB-C, power switch, EN/reset, Upper (GPIO34), Middle (GPIO35), Lower (GPIO39). GPIO34/35/39 are input-only and use PCB pull-ups; pressed is LOW.

## Product behavior that must be preserved

- Home page left: the shared Codex account's Weekly remaining quota, absolute reset time, selected source Agent, availability, and update time.
- Home page right: exactly three configured Agent devices, each with reachability, workload/activity, bounded task text, counts, and whether user action is required.
- The account quota source is Agent slot 0 first; another verified Agent may supply fallback account data because all three installations use the same Codex account. Slot 0 is fixed as primary.
- The Weekly bar shows remaining capacity: `100 - used_percent`; it starts full after reset and drains with use.
- Reset time is an absolute `YYYY-MM-DD HH:MM` time in UTC+8, not a countdown.
- Temperature and humidity belong on the System page, not Home.
- Battery percentage is voltage-derived and must remain visibly approximate (`~`).
- Home has no button legend, IP/subtitle row, or bottom help copy.
- Header contains the title, Wi-Fi icon, and a live `N/3` Agent count only. It must not show battery or a Bridge icon.
- Red is reserved for the title accent, low quota, and action/error/offline states.
- Short buttons: Upper next page, Middle refresh, Lower back.
- Long buttons: Upper immediate Agent poll, Middle System page, Lower Home.
- Use event-driven full refresh with the existing 30/60 second floors. Do not introduce periodic high-rate refresh or partial refresh without panel-specific proof.

## Architecture map

```text
board/  GPIO startup, buttons, SHT30, battery sampling
net/    Wi-Fi provisioning, private Agent settings/client, HTTP API
app/    fixed-size shared state and refresh policy
ui/     rendering and the only physical panel-refresh entry point
```

The HTTP task and Agent polling task run on Core 0. Button scanning and the
display state machine run from the Arduino main loop on Core 1. The legacy
`NetworkProbe` implementation is not started in `0.4.0-p1`. HTTP handlers only
mutate cached state or queue refreshes; they must never call the display
directly.

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

For hardware-visible changes, also perform a proportionate smoke test: serial boot with no panic, Wi-Fi/Agent health, button mapping, one physical refresh, and final `panel=powered_off`. Never publish raw network identifiers from that test.

## Agent boundary

The firmware consumes Codex Monitor Agent `0.4.x` schema `1.1` over HTTP. Every
JSON request uses `Authorization: Bearer <token>`. It verifies schema major 1
and a stable `installation_id` through `GET /api/v1/version`, then polls one of
three slots every five seconds with `GET /api/v1/status`; each Agent is visited
about every 15 seconds. It requests `GET /api/v1/threads?limit=12` only when
needed for current activity and polls account-level `GET /api/v1/rate-limits`
on a 60-second cadence, fixed slot 0 first with verified fallback.

An HTTP 401 locks that Agent until configuration changes. The first authenticated
identity is pinned in private NVS; an incompatible schema or later changed
installation identity is a protocol error and remains locked until configuration
changes. Ordinary transport failures move through
recovery and become offline after three consecutive failures. Do not collapse
unknown, stale, unavailable, idle, offline, auth error, and protocol error into
one state.

Three endpoints and one shared Bearer token are persisted in the project-owned
NVS blob only through the physical USB serial command
`AGENTS <endpoint0> <endpoint1> <endpoint2> <token>`. Slot 0 is always primary.
There is no network configuration endpoint; never reintroduce
`POST /api/agents` without owner review and an authenticated design. Agent
hosts, token, and installation IDs must stay in `SettingsStore`/`AgentClient`;
they must never enter `UiModel`, HTTP responses, renderer output, serial output,
or Git. The raw `AGENTS` input line is secret and must not be captured in logs
or responses. The complete consumed-field and provisioning contract is
documented in `docs/AGENT_PROTOCOL.md`. `docs/BRIDGE_PROTOCOL.md` is only a
compatibility pointer for the retired `0.3.x` Bridge path.

Do not vendor the external Agent program or the user's local Agent contract
copies into this repository. Private Agent settings may be changed only over a
locally controlled USB serial session. Never add them to Wi-Fi provisioning or
the device HTTP API.

## Repository hygiene

- Keep reverse-engineering dumps, downloaded vendor packages, SDKs, build caches, full flash images, and reference-project checkouts outside Git.
- Do not commit generated bootloader, partition, `boot_app0`, ELF, map, or object files.
- Do not track firmware binaries in normal Git history. The project is GPL-3.0-only; publish a binary only through a reviewed release that satisfies `THIRD_PARTY_NOTICES.md` and makes the corresponding source/license materials available. Local builds remain under ignored `dist/`.
- Preserve unrelated user work and local backups.
- Use small, reviewable commits and update documentation when pin maps, protocol fields, safety rules, or tool versions change.
- Prefer portable paths and POSIX shell for macOS support; keep PowerShell equivalents for Windows device work.

For renderer changes, make and inspect an 800x480 preview before building or flashing. For `EpdController`, BUSY handling, reset, power-off, or journal changes, raise the review and physical-test level; never validate those changes by UI inspection alone.

## Current open hardware work

- Visually accept the `0.4.0-p1` account/fleet layout on the physical panel.
- Test EN/reset during an active refresh and confirm bounded recovery without a refresh loop.
- Calibrate battery voltage/percentage against a multimeter.
- Compare SHT30 readings after USB/charger heat settles.
- Measure panel high-voltage behavior during a forced BUSY fault.
- Characterize current draw, low-voltage cutoff, sleep, and battery runtime before productization.
