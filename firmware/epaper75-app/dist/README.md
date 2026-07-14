# Local P1 build artifacts

Normative toolchain: Arduino ESP32 Core 3.3.10, 8 MB flash, DIO 40 MHz, no
PSRAM. `build_arduino.ps1` / `build_arduino.sh` generate the app image and
`SHA256SUMS.txt` in this directory.

For the already-backed-up target device, write only:

| File | Address | Purpose |
|---|---:|---|
| `epaper75-p1-app.bin` | `0x10000` | P1 app0 application |

Generated binaries and checksums are intentionally ignored by normal Git
history. Publish them only through a reviewed release that includes the
GPL-3.0-only license, corresponding source/build instructions, and the notices
required by `THIRD_PARTY_NOTICES.md`.

The build scripts discard generated bootloader, partition, and boot_app0
artifacts. Do not rewrite the bootloader, partition table, OTA metadata, NVS,
app1, or SPIFFS during P1 testing.
