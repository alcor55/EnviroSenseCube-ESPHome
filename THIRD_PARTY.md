# Third-party material

Apache-2.0 applies to the original EnviroSenseCube code, configuration and
documentation. It does not relicense material belonging to other authors.
Existing source headers and attribution documents are preserved.

## Included Bosch wrapper

- Files: `components/envirosense_cube/sleep_aware_bsec2.h` and `.cpp`.
- Copyright (c) 2021 Bosch Sensortec GmbH. All rights reserved.
- License: [BSD-3-Clause](licenses/BSD-3-Clause-Bosch.txt).
- Source: [Bosch-BSEC2-Library, tag 1.10.2610](https://github.com/boschsensortec/Bosch-BSEC2-Library/tree/1.10.2610).
- Local modifications: [BSEC_WRAPPER_NOTICE.md](components/envirosense_cube/BSEC_WRAPPER_NOTICE.md).

## Included Pervasive Displays tri-colour driver

- Files: `components/envirosense_cube/E2154JS0C1_lib.cpp` and `.h`.
- Source: [PDLS_EXT3_Basic_Global](https://github.com/PervasiveDisplays/PDLS_EXT3_Basic_Global),
  reference inspected `ca37a59` (2025-01-31).
- Copyright (c) Rei Vilo, 2010-2025; portions (c) Pervasive Displays, 2010-2025.
- License: **CC BY-SA 4.0**, not Apache-2.0. The original
  [LICENCE.md is retained here](licenses/CC-BY-SA-4.0-PDI.md).
- The local implementation is a modified minimal ESP8266 port of the Film J /
  driver C global-update sequence, adapted to the project's GPIO and SPI
  configuration and bounded operation timeouts.
- Upstream also states: **For exclusive use with Pervasive Displays screens**.
  This statement is preserved in NOTICE, not silently removed or replaced by
  Apache-2.0. Before other hardware use or binary redistribution, review
  these upstream terms; this project does not resolve their interaction
  with CC BY-SA's no-additional-restrictions provision.

## Dependencies downloaded during compilation

These dependencies are not relicensed by the project's LICENSE file. Consult
the terms accompanying the precise versions used by the build, including
any obligations affecting redistribution of compiled firmware.

- [Bosch BSEC2](https://github.com/boschsensortec/Bosch-BSEC2-Library/tree/1.10.2610):
  the algorithm binary has separate Bosch software terms. Its use is not
  covered by the BSD license of the Arduino wrapper.
- [Bosch BME68x driver](https://github.com/boschsensortec/Bosch-BME68x-Library):
  retain upstream license and copyright notices.
- [ESPHome](https://github.com/esphome/esphome/blob/2025.7.5/LICENSE).
- [ESP8266 Arduino core](https://github.com/esp8266/Arduino/tree/3.1.2):
  see its COPYING files and the notices of bundled components.
- [Adafruit LC709203F](https://github.com/adafruit/Adafruit_LC709203F).
- [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library).
- [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO).
- [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor).

The firmware build can include additional transitive dependencies, each of
which retains its own terms. This source-repository notice is not a substitute
for the license notices required when distributing a firmware binary.
