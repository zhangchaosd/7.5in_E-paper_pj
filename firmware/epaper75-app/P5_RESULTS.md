# P5 results: 0.6.0-p1 calendar + QWeather, deep sleep

Date: 2026-11-22. All identifiers (port, network, API Host, IDs, keys,
coordinates) are omitted.

## Pre-flash findings

- Two fresh 8 MB factory backups were read at 115200 baud (460800 failed with
  serial noise behind a USB hub chain). They differ only inside NVS
  (0x9000–0xDFFF) because the firmware booted and wrote NVS between reads;
  bootloader, partition table, otadata, app0, app1 and SPIFFS are identical.
- The device was **not** running `0.5.1-p1` as previously documented. app0
  held an unrecorded build reporting `0.6.0-p1` (HTTP API present, no `AGENTS`
  command) whose source is not in this repository. Its app0 image is kept,
  ignored by Git, next to the backups (`app0-preexisting-unrecorded-0.6.0-p1.bin`,
  SHA-256 `61AD5772…2D119`) as a rollback option, and is also inside both
  factory backups.
- `tools/flash_app0_only.sh` failed on macOS bash 3.2 (`set -u` with an empty
  array) before writing anything; fixed with the portable
  `${array[@]+"${array[@]}"}` form.

## Flash

| Step | Result |
|---|---|
| Pre-flash `INFO` | `panel=powered_off error=none` (both flashes) |
| Write | app0 only, `0x10000`, 115200 baud, hash verified |
| Image | final 2,074,080 bytes (85.5 % of app0), ESP32 / 8MB / DIO / 40m |

The first 0.6.0 flash showed TLS connection failures: response buffers (128 KB)
were allocated before the handshake. Buffers are now allocated after the TLS
session is established and sized from Content-Length / the gzip trailer.

## Smoke test

- Cold boot without panic; Wi-Fi credentials stored by the driver survived the
  app0 flash (`wifi_saved=yes`); NTP synchronised.
- QWeather provisioned over USB serial with `tools/provision_qweather.sh`
  (`[qweather] settings stored`, `[location] stored`).
- First live fetch: `weather=available last_error=none` (JWT, verified TLS,
  gzip and current/daily parsing work against the real API).
- Full refresh 20.3–20.5 s; final `panel=powered_off error=none`.
- Battery reading 4.18–4.24 V on USB; SHT30 ok.

- Owner visually confirmed the rendered Home page is correct.

## Button wake test

| Action | Result |
|---|---|
| Lower while asleep | no wake (expected: GPIO39 is not a wake source) |
| Middle while asleep | `cause=2` (ext0), fetch + full refresh, slept after 90 s |
| Upper while asleep | `cause=3` (ext1), System page, back to Home after 90 s, slept |

This test exposed a real defect: after the Upper wake the weather cache read
`unknown` although the wake counter survived. `RtcState` had members with
default initializers, so the C++ runtime re-ran its constructor on every
deep-sleep wake and reset the cached weather/place while leaving plain
counters untouched. It now lives in raw `RTC_DATA_ATTR` storage and is
constructed only on a cold boot.

## Owner-requested changes after the first visual check

- No "和风天气" wording on screen (header shows only "HH:MM 更新"; the
  System page source row was removed).
- Indoor temperature/humidity removed from both pages; the SHT30 is no longer
  sampled (battery divider only). The Home facts grid shows today's
  precipitation chance instead.

## Timer wake (final build, SHA-256 `F3B65F27…606114`)

- 21:59:58 timer wake (`cause=4`, 2 s early, inside tolerance) for the 22:00
  alert check: NTP synchronised, 0 active alerts, `[display] unchanged; no
  refresh` (cache survived deep sleep), then deep sleep for 121 min until the
  00:02 date change. Whole wake about 15 s with the radio on.

## Still to verify

- A full-update timer wake that changes the page (e.g. 00:02 date change).
- GPIO25 held LOW during sleep (meter check).
- Deep-sleep current and per-wake energy.
- `[clock]` reporting after the SNTP-callback change (included in this build).
