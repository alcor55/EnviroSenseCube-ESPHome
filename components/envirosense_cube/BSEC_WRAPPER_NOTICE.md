# Sleep-aware Bosch wrapper

`sleep_aware_bsec2.h/.cpp` are derived from Bosch Sensortec's BSD-3-Clause
Arduino wrapper, tag `1.10.2610`, commit
`0dac6ea22739ed9d91442da5283f36133b86b771`:
https://github.com/boschsensortec/Bosch-BSEC2-Library/tree/1.10.2610/src

The original copyright and license text is retained in each file.
Local changes are limited to:

- Renaming `Bsec2` and its callback to avoid colliding with the upstream wrapper.
- Adding a 64-bit boot time offset and a getter for the next BSEC call deadline.
- Adding that offset to `getTimeMs()` and fixing its overflow increment to 2^32.

The proprietary BSEC algorithm, configuration profile, state format, acquisition
sequence and sensor driver are unchanged. A separate 24-byte RTC clock record
is matched to the state blob hash. It counts planned sleep only after an actual
deep-sleep reset. On power loss or a flash-state fallback, a new time reference
is used without invalidating the saved calibration.

This addresses a missing time reference; successful compilation and host clock
tests do not establish that BSEC accuracy survives repeated resets. That must
be verified on the physical device with OTA maintenance disabled.
