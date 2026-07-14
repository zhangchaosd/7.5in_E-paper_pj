# Third-party dependencies

This project is distributed under `GPL-3.0-only`; see `LICENSE`. The repository does not vendor the Arduino core or library source. The setup scripts install these pinned build dependencies from their upstream package indexes:

| Dependency | Version | Upstream license |
|---|---:|---|
| Arduino core for ESP32 | 3.3.10 | LGPL-2.1-or-later and component-specific terms |
| GxEPD2 | 1.6.9 | GPL-3.0-only |
| Adafruit GFX Library | 1.12.6 | BSD-3-Clause-style Adafruit license |
| Adafruit BusIO | 1.17.4 | MIT |
| ArduinoJson | 7.4.3 | MIT |
| WiFiManager | 2.0.17 | MIT |

Upstream projects:

- <https://github.com/espressif/arduino-esp32>
- <https://github.com/ZinggJM/GxEPD2>
- <https://github.com/adafruit/Adafruit-GFX-Library>
- <https://github.com/adafruit/Adafruit_BusIO>
- <https://github.com/bblanchon/ArduinoJson>
- <https://github.com/tzapu/WiFiManager>

`firmware/epaper75-app/src/ui/EpdController.cpp` adapts the panel command and write sequence from `GxEPD2_750c_Z08`, authored by Jean-Marc Zingg. Adafruit GFX and BusIO retain the copyright and disclaimer terms supplied by Adafruit Industries. Other dependencies retain their respective upstream notices.

Any precompiled firmware release must satisfy the project GPLv3 terms and all applicable source, copyright, disclaimer, and license-text obligations of the pinned dependencies. Re-check the exact upstream license files when preparing a release and include the corresponding source/build instructions. This notice is an engineering checklist, not a substitute for the upstream terms or legal advice.
