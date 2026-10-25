#include "components/envirosense_cube/bsec_clock.h"

#include <cassert>
#include <cstdio>

using namespace esphome::envirosense_cube;

int main() {
  constexpr uint32_t hash = 0x12345678;
  BsecClockState clock{15000, 285000, hash, BSEC_CLOCK_VERSION, 0};
  assert(bsec_clock_matches(clock, hash));
  assert(!bsec_clock_matches(clock, hash + 1));
  assert(bsec_clock_resume(clock, true) == 300000);
  assert(bsec_clock_resume(clock, false) == 15000);  // OTA reset did not sleep.
  clock.version = 0;
  assert(!bsec_clock_matches(clock, hash));
  clock.version = BSEC_CLOCK_VERSION;
  clock.planned_sleep_ms = 300001;
  assert(!bsec_clock_matches(clock, hash));

  // Twenty years of five-minute wakeups, including the 32-bit millis boundary.
  uint64_t previous_sample = 0;
  uint64_t boot_base = 0;
  for (uint32_t cycle = 0; cycle < 2102400; cycle++) {
    const uint64_t sample = boot_base + 200;
    if (cycle != 0)
      assert(sample - previous_sample == 300200);
    clock = {boot_base + 15000, 285000, hash, BSEC_CLOCK_VERSION, 0};
    assert(bsec_clock_matches(clock, hash));
    boot_base = bsec_clock_resume(clock, true) + 200;  // boot overhead counted.
    previous_sample = sample;
  }
  assert(boot_base > UINT64_C(0xFFFFFFFF));
  std::puts("BSEC clock tests passed: deep sleep, OTA reset, state mismatch, 32-bit boundary");
}
