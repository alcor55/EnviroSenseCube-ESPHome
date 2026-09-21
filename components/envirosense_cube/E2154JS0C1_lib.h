#pragma once

#include <Arduino.h>

namespace esphome {
namespace envirosense_cube {
namespace e2154js0c1 {

static constexpr uint16_t WIDTH = 152;
static constexpr uint16_t HEIGHT = 152;
static constexpr size_t PLANE_SIZE = WIDTH * HEIGHT / 8;

void begin_operation(uint32_t timeout_ms);
void end_operation();
bool operation_timed_out();

// The black plane uses 1 = white and 0 = black. The red plane uses
// 1 = red and 0 = no red, matching Pervasive Displays' Film J driver.
bool update(const uint8_t *black_plane, const uint8_t *red_plane, int8_t temperature_c);
void force_power_off();

}  // namespace e2154js0c1
}  // namespace envirosense_cube
}  // namespace esphome
