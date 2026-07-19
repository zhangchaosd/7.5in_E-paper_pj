# P3 refresh-strategy validation

Date: 2026-07-19
Firmware: `0.5.0-p1`

This record covers the move from global-only refresh to bounded partial-window
updates. Network identifiers, serial-port identity, credentials and Agent
installation identities are intentionally omitted.

## Panel capability and design

- The pinned `GxEPD2_750c_Z08` driver declares partial-window support and
  explicitly declares that fast partial update is unavailable.
- Full and partial paths both use the panel's `0x12` update command and an
  approximately 18-second driver timing budget. Partial refresh is used to
  reduce the area that visibly flashes, not to promise a shorter waveform.
- Visible model changes map to fixed byte-aligned regions. All regions pending
  in one cycle are merged into one window, so a cycle never causes several
  consecutive partial waveforms.
- First boot, page changes, manual cleanup, a merged window covering at least
  72% of the screen, 24 accumulated partial updates, or six hours since the
  last full cleanup select a full refresh.
- The existing 30/60-second floors, bounded BUSY waits, RTC journal, one-retry
  rule, final power-off and fault lock remain in force. A failed partial attempt
  promotes its only recovery attempt to a full refresh.

## Build and static validation

- Canonical Arduino ESP32 core `3.3.10` build passed with all pinned libraries.
- App image size: `1,301,600` bytes, below the app0 limit `0x250000`.
- Static RAM: `67,216` bytes (20% of 327,680), with no PSRAM required.
- Generated SHA-256 manifest matched the app image.
- `esptool image-info` confirmed ESP32, 8 MB flash, DIO and 40 MHz.

## Physical smoke test

- The pre-flash serial gate reported the known `0.4.0-p1` baseline,
  `panel=powered_off`, `error=none`, and no panic/brownout evidence.
- Both independent 8 MB factory backups passed the app0 helper's size and
  SHA-256 checks. The approved helper wrote only the application at `0x10000`;
  no erase or other partition write was performed.
- `0.5.0-p1` booted and completed the required first full refresh to establish
  an in-boot display baseline. It then returned to `panel=powered_off` with no
  display error, panic or brownout.
- A later real Agent data change selected and completed an actual partial
  refresh. The controller did not promote it to full, the partial-since-full
  counter advanced to one, and the panel again returned to powered off with no
  error.
- Wi-Fi, all three Agent slots and account data remained healthy after the
  partial refresh. Button mapping and the Agent/API protocol were not changed.
- The exact 800x480 Home and System previews both reported zero text overflow
  and zero text-box overlap.

The owner should still visually confirm that the physical partial waveform did
not visibly disturb pixels outside the selected window. Long-run residue at the
24-partial/six-hour cleanup thresholds also remains an observation item rather
than a one-cycle smoke-test result.
