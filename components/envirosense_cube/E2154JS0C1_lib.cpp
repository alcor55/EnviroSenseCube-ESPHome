#include "E2154JS0C1_lib.h"

#include <SPI.h>

#include "esphome/core/log.h"

namespace esphome {
namespace envirosense_cube {
namespace e2154js0c1 {

// Minimal ESP8266 port of the official Pervasive Displays Film J / driver C
// global-update sequence for E2154JS0C1. The reference implementation is
// PDLS_EXT3_Basic_Global, licensed CC BY-SA 4.0:
// https://github.com/PervasiveDisplays/PDLS_EXT3_Basic_Global

static const char *const TAG = "envirosense_epaper";

static constexpr uint8_t BUSY_PIN = 12;
static constexpr uint8_t RESET_PIN = 0;
static constexpr uint8_t DC_PIN = 15;
static constexpr uint8_t CS_PIN = 2;

static uint32_t operation_deadline_ms = 0;
static bool operation_active = false;
static bool operation_timeout = false;

static bool deadline_reached_() {
  return operation_active && static_cast<int32_t>(millis() - operation_deadline_ms) >= 0;
}

void begin_operation(uint32_t timeout_ms) {
  operation_deadline_ms = millis() + timeout_ms;
  operation_active = true;
  operation_timeout = false;
}

void end_operation() { operation_active = false; }

bool operation_timed_out() { return operation_timeout || deadline_reached_(); }

static void write_command_(uint8_t command) {
  digitalWrite(DC_PIN, LOW);
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(command);
  digitalWrite(CS_PIN, HIGH);
}

static void write_command_data_(uint8_t command, uint8_t data) {
  digitalWrite(DC_PIN, LOW);
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(command);
  digitalWrite(DC_PIN, HIGH);
  SPI.transfer(data);
  digitalWrite(CS_PIN, HIGH);
}

static void write_command_buffer_(uint8_t command, const uint8_t *data, size_t length) {
  // The reference driver releases CS between the command and data phases.
  digitalWrite(DC_PIN, LOW);
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(command);
  digitalWrite(CS_PIN, HIGH);

  digitalWrite(DC_PIN, HIGH);
  digitalWrite(CS_PIN, LOW);
  for (size_t i = 0; i < length; i++) {
    SPI.transfer(data[i]);
    if ((i & 0xFF) == 0)
      yield();
  }
  digitalWrite(CS_PIN, HIGH);
}

static bool wait_ready_(const char *stage) {
  const uint32_t started_at = millis();
  while (digitalRead(BUSY_PIN) == LOW) {
    if (deadline_reached_()) {
      operation_timeout = true;
      ESP_LOGE(TAG, "E2154JS0C1 BUSY timeout during %s after %lu ms", stage,
               millis() - started_at);
      return false;
    }
    delay(10);
  }
  ESP_LOGD(TAG, "E2154JS0C1 BUSY released during %s after %lu ms", stage,
           millis() - started_at);
  return true;
}

static void reset_() {
  // Official small Film C/J reset timing: 5, 5, 10, 5, 5 ms.
  delay(5);
  digitalWrite(RESET_PIN, HIGH);
  delay(5);
  digitalWrite(RESET_PIN, LOW);
  delay(10);
  digitalWrite(RESET_PIN, HIGH);
  delay(5);
  digitalWrite(CS_PIN, HIGH);
  delay(5);
}

static bool initialize_(int8_t temperature_c) {
  if (temperature_c < 0 || temperature_c > 40) {
    ESP_LOGW(TAG, "E2154JS0C1 refresh skipped outside the supported 0..40 C range: %d C",
             temperature_c);
    return false;
  }

  reset_();

  write_command_data_(0x00, 0x0E);  // Soft reset.
  delay(5);

  write_command_data_(0xE5, static_cast<uint8_t>(temperature_c));  // Input temperature.
  write_command_data_(0xE0, 0x02);                 // Use active temperature.

  static const uint8_t panel_setting[] = {0xCF, 0x8D};
  write_command_buffer_(0x00, panel_setting, sizeof(panel_setting));
  return !deadline_reached_();
}

void force_power_off() {
  // Best-effort DC/DC shutdown. Do not wait again after a timeout.
  write_command_(0x02);
  digitalWrite(CS_PIN, HIGH);
}

bool update(const uint8_t *black_plane, const uint8_t *red_plane, int8_t temperature_c) {
  if (black_plane == nullptr || red_plane == nullptr)
    return false;

  if (!initialize_(temperature_c))
    return false;

  write_command_buffer_(0x10, black_plane, PLANE_SIZE);
  write_command_buffer_(0x13, red_plane, PLANE_SIZE);
  if (deadline_reached_()) {
    operation_timeout = true;
    return false;
  }

  if (!wait_ready_("pre-power-on"))
    return false;
  write_command_(0x04);  // Power on.
  if (!wait_ready_("power-on"))
    return false;

  write_command_(0x12);  // Global display refresh.
  delay(5);
  if (!wait_ready_("global refresh"))
    return false;

  write_command_(0x02);  // Turn off DC/DC.
  return wait_ready_("power-off");
}

}  // namespace e2154js0c1
}  // namespace envirosense_cube
}  // namespace esphome
