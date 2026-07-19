# P4 full-only recovery validation

Date: 2026-07-19
Firmware: `0.5.1-p1`

This record documents the response to the physical half-screen shadow produced
by the withdrawn `0.5.0-p1` partial-window experiment. Network identifiers,
serial-port identity, credentials and Agent installation identities are
intentionally omitted.

## Root cause and recovery

- The affected device reported 17 completed partial updates, a powered-off
  panel and no controller error while the owner observed half-screen shadowing.
  The panel was not stuck mid-refresh; repeated partial waveforms produced an
  unacceptable physical image.
- One explicit full refresh cleared the controller's partial counter and
  completed with `panel=powered_off` and `error=none`.
- While the corrective build was running, the old firmware completed four more
  partial updates. This confirmed the issue could recur quickly and that merely
  lowering the 24-update cleanup threshold was not an adequate fix.

## Corrective firmware

- `EpdController` again accepts only full-width page writes and rejects partial
  mode/window refresh calls. There is no partial plan in `RefreshPolicy`.
- Routine visible changes are batched behind a five-minute floor. New approval,
  input, offline and error transitions use a 60-second floor; explicit refresh
  and page changes use the existing 30-second floor.
- Changes not visible on the current page, timestamp-only polls and RSSI jitter
  cannot schedule a physical refresh.
- The System page reports `DISPLAY   FULL / 5 MIN BATCHED`.

## Build, flash and smoke test

- Canonical Arduino ESP32 core `3.3.10` build passed with the pinned libraries.
- App image size: `1,299,840` bytes, below the app0 limit `0x250000`.
- Static RAM: `67,200` bytes (20% of 327,680), with no PSRAM required.
- The exact 800x480 System preview reported zero text overflow and zero
  text-box overlap.
- The pre-flash gate reported `panel=powered_off`, `error=none` and no panic or
  brownout evidence. Both independent 8 MB factory backups passed verification,
  and the approved helper wrote only app0 at `0x10000`.
- `0.5.1-p1` booted, performed one recovery full refresh, returned to
  `panel=powered_off`, and showed no display error, panic, brownout or partial
  refresh evidence.

Owner visual confirmation that the recovery full refresh removed the shadow is
the remaining acceptance step.
