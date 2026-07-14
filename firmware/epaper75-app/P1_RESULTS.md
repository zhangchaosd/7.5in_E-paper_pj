# P1 verification record

## Completed

- Independent P1 project created; P0 diagnostic and both factory backups are
  unchanged.
- Arduino ESP32 Core 3.3.10 build passes with pinned libraries:
  GxEPD2 1.6.9, Adafruit GFX 1.12.6, Adafruit BusIO 1.17.4,
  ArduinoJson 7.4.3, and WiFiManager 2.0.17.
- Final v0.3.0 app image: 1,206,144 bytes, below app0's
  0x250000-byte limit. The linked sketch payload is 1,205,996 bytes.
- Static RAM: 65,240 bytes (19% of 327,680); no PSRAM is required.
- Application image validates as ESP32, 8 MB flash, DIO, 40 MHz.
- Generated P1 partition binary is byte-identical to the P0-proven partition
  binary. Geometry remains:
  NVS 0x9000, OTA data 0xE000, app0 0x10000, app1 0x260000, SPIFFS 0x4B0000.
- Independent reviews found no remaining blocker after the safe Z08 BUSY path,
  RTC journal, JSON numeric parsing, refresh-policy corrections, and moving the
  potentially blocking DNS/TCP probe off the button-scanning task.
- The app0 flashing script verifies the P1 checksum plus both 8 MB factory
  backup hashes before contacting the device.
- The final v0.3.0 app was written only to app0 at 0x10000; esptool verified
  the written hash and left every other partition untouched.
- Stored Wi-Fi and the P1 Bridge endpoint both survive app0 updates. Runtime
  reports 8 MB flash, zero PSRAM, connected Wi-Fi, `powered_off` panel state,
  and no display error.
- First migration from the P0 hibernated panel exercised the conservative
  Unknown/BUSY path: 50-second wait, one reset/init, 20.964-second refresh,
  clean power-off. Subsequent refreshes start with BUSY HIGH and complete in
  approximately 21.0-21.1 seconds without recovery.
- `/api/info` and `/api/health` respond correctly. Five health requests made
  during a physical refresh all succeeded in 52-368 ms. Invalid `net` mode and
  URL-form Bridge values return 400; unknown routes return 404.
- During the original Windows verification, official .NET SDK 8.0.422 built the
  unmodified external reference Bridge in Release mode with zero warnings and
  zero errors. The SDK/cache and external sample source are intentionally not
  part of this repository.
- Live Bridge integration passes: `/status` is valid, the device reports the
  Bridge configured and reachable, Claude is currently offline, and Codex data
  is live. A 60-second run had 12/12 reachable samples and zero serial timeout
  warnings.
- A short Bridge process restart remained reachable as designed, proving the
  three-consecutive-failure debounce; the restarted Bridge is listening now.
- v0.3.0 renders only Codex usage on the left. Both bars are remaining quota
  (`100 - used`), and reset times are stable absolute UTC+8 clock times. Claude
  remains accepted in the compatibility JSON but is not rendered.
- The right half now shows DNS + TCP/443 connect latency for NetEase Cloud
  Music, GitHub, Google, and YouTube. All four were sampled successfully on the
  flashed device. The probe runs in a low-priority Core 0 task; the main button
  loop never performs DNS or TCP work.
- The screen receives network semantic changes immediately but ignores raw
  millisecond jitter, with an exact-value refresh every five completed rounds
  (about five minutes). This avoids near-continuous full refresh.
- The device API reports `showing=codex_network`, Bridge reachable, all four
  targets sampled, and multiple completed probe rounds. Runtime battery was
  approximately 4.27 V and displayed as `~100%`; this remains an uncalibrated
  voltage-derived estimate, not fuel-gauge data.
- The user confirmed the v0.2.0 Codex/network screen and all button behavior.
  v0.3.0 keeps the compact one-line header but fully replaces the card-heavy
  body: large quota percentages/bars on the left and a continuous, separator-
  based latency table on the right. The old subtitle/IP row and all home-page
  bottom notes remain removed without changing button or data logic.
- Post-flash serial verification reports firmware `0.3.0-p1`, page 0,
  `powered_off`, and no panel error after the physical refresh.

Device-verified v0.3.0 application SHA-256:

```text
CAFB1A3511AAB2E644C28EAC68B7847185037E9E2F95045BEB2228123CF1A983
```

## Remaining manual/hardware acceptance

1. User visually confirms the v0.3.0 flat quota/table composition.
2. Deliberately test EN during a refresh and verify that startup waits or
   reinitializes once without a refresh loop.
3. Calibrate battery voltage against a multimeter and compare SHT30 temperature
   with ambient after USB/charging heat has settled.
4. Measure panel high-voltage rails or current during a forced BUSY fault before
   treating the fallback reset pulse as product-certified shutdown.

## Known P1 limitations

- Buttons and outgoing Bridge polling pause during a physical full refresh;
  the device HTTP API and Core 0 network probe do not.
- A short press entirely inside the refresh window can be missed.
- Wi-Fi reconnect after the first successful connection does not automatically
  reopen the provisioning portal.
- `net`/`music`, GIF/sprites, a root web page, OTA, partial refresh, sleep, and
  low-battery shutdown are deferred.
- Battery voltage is uncalibrated and the SHT30 temperature may include local
  PCB/charging heat.
- The displayed network number is DNS + TCP/443 connect time, not ICMP ping,
  TLS negotiation, or complete page-load time.
- BUSY-fault high-voltage shutdown still needs an oscilloscope or rail-current
  test for product certification.
