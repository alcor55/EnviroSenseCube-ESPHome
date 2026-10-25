#pragma once

#include <cstdint>

namespace esphome {
namespace envirosense_cube {

// Independent RTC record: do not change SupportData or existing preference keys.
struct BsecClockState {
  uint64_t elapsed_ms;
  uint32_t planned_sleep_ms;
  uint32_t state_hash;
  uint32_t version;
  uint32_t reserved;
};
static constexpr uint32_t BSEC_CLOCK_VERSION = 0x42544301;
static_assert(sizeof(BsecClockState) == 24, "BSEC clock RTC record changed");

inline bool bsec_clock_matches(const BsecClockState &clock, uint32_t state_hash) {
  return clock.version == BSEC_CLOCK_VERSION && clock.state_hash == state_hash &&
         clock.planned_sleep_ms <= 300000 && clock.elapsed_ms < (UINT64_C(1) << 60);
}

inline uint64_t bsec_clock_resume(const BsecClockState &clock, bool deep_sleep_wake) {
  // A planned sleep must not be counted on OTA/software reset or power loss.
  return clock.elapsed_ms + (deep_sleep_wake ? clock.planned_sleep_ms : 0);
}

}  // namespace envirosense_cube
}  // namespace esphome
