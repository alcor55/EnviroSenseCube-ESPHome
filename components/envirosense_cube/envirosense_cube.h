#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_LC709203F.h>
#include <Wire.h>
#include "sleep_aware_bsec2.h"
#include "bsec_clock.h"

#include "esphome/components/api/api_server.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/deep_sleep/deep_sleep_component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace envirosense_cube {

class PersistentLC709203F : public Adafruit_LC709203F {
 public:
  bool begin_preserving_state(TwoWire *wire = &Wire);
  bool read_cell(float &voltage, float &percent);
  bool set_cell_temperature(float temperature_c);
};

class EnviroSenseCube : public Component {
 public:
  EnviroSenseCube();

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void notify_api_client_connected(const std::string &client_info, const std::string &client_address);
  void notify_api_client_disconnected(const std::string &client_info, const std::string &client_address);
  void set_diagnostic_logging(bool enabled);
  void set_ota_maintenance(bool enabled);
  void request_display_refresh();
  void save_bsec_state_to_flash();
  void reset_bsec_state();
  void restore_bsec_backup();

  void set_deep_sleep(deep_sleep::DeepSleepComponent *deep_sleep) { this->deep_sleep_ = deep_sleep; }
  void set_temperature_offset(float value) { this->temperature_offset_ = value; }
  void set_humidity_offset(float value) { this->humidity_offset_ = value; }
  void set_pressure_offset(float value) { this->pressure_offset_ = value; }
  void set_temperature_threshold(float value) { this->temperature_threshold_ = value; }
  void set_humidity_threshold(float value) { this->humidity_threshold_ = value; }
  void set_pressure_threshold(float value) { this->pressure_threshold_ = value; }
  void set_static_iaq_threshold(float value) { this->static_iaq_threshold_ = value; }
  void set_battery_threshold(float value) { this->battery_threshold_ = value; }
  void set_temperature_sensor(sensor::Sensor *value) { this->temperature_sensor_ = value; }
  void set_humidity_sensor(sensor::Sensor *value) { this->humidity_sensor_ = value; }
  void set_pressure_sensor(sensor::Sensor *value) { this->pressure_sensor_ = value; }
  void set_gas_resistance_sensor(sensor::Sensor *value) { this->gas_resistance_sensor_ = value; }
  void set_static_iaq_sensor(sensor::Sensor *value) { this->static_iaq_sensor_ = value; }
  void set_adaptive_iaq_sensor(sensor::Sensor *value) { this->adaptive_iaq_sensor_ = value; }
  void set_iaq_accuracy_sensor(sensor::Sensor *value) { this->iaq_accuracy_sensor_ = value; }
  void set_co2_equivalent_sensor(sensor::Sensor *value) { this->co2_equivalent_sensor_ = value; }
  void set_breath_voc_equivalent_sensor(sensor::Sensor *value) { this->breath_voc_equivalent_sensor_ = value; }
  void set_battery_sensor(sensor::Sensor *value) { this->battery_sensor_ = value; }
  void set_battery_raw_sensor(sensor::Sensor *value) { this->battery_raw_sensor_ = value; }
  void set_battery_voltage_sensor(sensor::Sensor *value) { this->battery_voltage_sensor_ = value; }
  void set_battery_valid_sensor(binary_sensor::BinarySensor *value) { this->battery_valid_sensor_ = value; }
  void set_bsec_stabilized_sensor(binary_sensor::BinarySensor *value) { this->bsec_stabilized_sensor_ = value; }
  void set_bsec_run_in_sensor(binary_sensor::BinarySensor *value) { this->bsec_run_in_sensor_ = value; }
  void set_ota_maintenance_sensor(binary_sensor::BinarySensor *value) { this->ota_maintenance_sensor_ = value; }

 protected:
  static constexpr uint32_t RTC_STATE_KEY = 0xEC5CE502;
  static constexpr uint32_t BSEC_FLASH_STATE_KEY = 0xEC5CE503;
  static constexpr uint32_t BSEC_FLASH_VALID_KEY = 0xEC5CE504;
  static constexpr uint32_t BSEC_CLOCK_RTC_KEY = 0xEC5CE505;
  static constexpr uint8_t BSEC_FLASH_STATE_VALID = 0xA5;
  static constexpr uint8_t BSEC_FLASH_STATE_INVALID = 0x00;
  static constexpr uint8_t BATTERY_HISTORY_VALUES = 8;
  static constexpr uint8_t BATTERY_SAMPLES = 3;
  // Film J only supports a slow global refresh. Coalesce measurement changes
  // for at least 30 minutes and force a refresh every six hours.
  static constexpr uint8_t DISPLAY_MIN_CYCLES = 6;
  static constexpr uint8_t DISPLAY_FULL_REFRESH_CYCLES = 72;
  static constexpr uint8_t HEARTBEAT_CYCLES = 6;
  static constexpr uint8_t BSEC_FLASH_CHECKPOINT_CYCLES = 72;
  static constexpr uint8_t BSEC_FLASH_NOT_SAVED = 0xFF;

  enum SupportFlag : uint8_t {
    FLAG_DISPLAY_UPDATE_PENDING = 1 << 0,
    FLAG_PUBLISH_REFERENCE_VALID = 1 << 1,
    FLAG_PUBLISH_BATTERY_REFERENCE_VALID = 1 << 2,
    FLAG_OTA_MAINTENANCE_REQUESTED = 1 << 3,
    FLAG_BSEC_STATE_VALID = 1 << 4,
    FLAG_BSEC_QUALITY_CHECKPOINT_SAVED = 1 << 5,
    FLAG_LAST_STATIC_IAQ_VALID = 1 << 6,
    FLAG_BATTERY_HISTORY_VALID = 1 << 7,
  };

  enum DisplaySnapshotFlag : uint8_t {
    DISPLAY_SNAPSHOT_VALID = 1 << 0,
    DISPLAY_SNAPSHOT_BATTERY_VALID = 1 << 1,
    DISPLAY_SNAPSHOT_IAQ_VALID = 1 << 2,
  };
  static constexpr uint8_t DISPLAY_SNAPSHOT_LAYOUT_MASK = 0xF0;
  static constexpr uint8_t DISPLAY_SNAPSHOT_LAYOUT_CURRENT = 0x00;

  struct DisplaySnapshot {
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint16_t pressure_x10;
    uint16_t static_iaq;
    uint8_t battery_x2;
    uint8_t flags;
  };

  // Ordered to avoid alignment padding. Removing graph histories leaves only
  // the latest IAQ plus the values needed to reconstruct the previous screen.
  struct SupportData {
    int16_t publish_temperature_x10;
    uint16_t publish_humidity_x10;
    uint16_t publish_pressure_x2;
    uint16_t publish_static_iaq;
    DisplaySnapshot display_snapshot;
    uint8_t publish_battery_x2;
    uint8_t last_static_iaq_x2;
    uint8_t battery_history[BATTERY_HISTORY_VALUES];
    uint8_t flags;
    uint8_t display_cycles_since_update;
    uint8_t cycles_since_publish_attempt;
    uint8_t display_update_count;
    uint8_t bsec_flash_cycles;
    uint8_t bsec_state[BSEC_MAX_STATE_BLOB_SIZE];
  };

  static_assert(sizeof(DisplaySnapshot) == 10, "DisplaySnapshot must remain compact");
  static_assert(sizeof(SupportData) == 272, "SupportData must fit the planned ESP8266 RTC budget");

  enum RunState : uint8_t {
    STARTING,
    OTA_WINDOW,
    WAITING_FOR_BSEC,
    WAITING_FOR_WIFI,
    WAITING_FOR_API,
    WAITING_FOR_API_FLUSH,
    WAITING_FOR_NEXT_CYCLE,
    FINISHED,
  };

  bool flag_(SupportFlag flag) const { return (this->support_data_.flags & flag) != 0; }
  void set_flag_(SupportFlag flag, bool enabled);
  bool migrate_legacy_support_data_();
  void initialize_support_data_();
  void save_support_data_();
  bool save_bsec_clock_(uint32_t planned_sleep_ms = 0);
  void ensure_bsec_backup_();
  bool mount_bsec_filesystem_();
  bool read_bsec_backup_(uint8_t *state);
  bool initialize_sensors_();
  bool initialize_bsec_();
  bool acquire_bsec_measurement_();
  bool validate_measurements_();
  void complete_measurement_cycle_();
  bool read_battery_();
  bool restore_bsec_state_();
  void capture_bsec_state_();
  void checkpoint_bsec_state_();
  static void bsec_data_callback_(const bme68xData data, const bsecOutputs outputs, SleepAwareBsec2 bsec);
  void handle_bsec_data_(const bme68xData &data, const bsecOutputs &outputs);
  void update_last_static_iaq_();
  bool environment_threshold_exceeded_() const;
  bool battery_threshold_exceeded_() const;
  void store_publish_attempt_reference_();
  void publish_states_();
  void reset_cycle_measurements_();
  void start_cycle_();
  void start_continuous_bsec_cycle_();
  void abort_cycle_();
  void finish_cycle_();
  void enter_sleep_();

  int16_t encode_temperature_reference_(float value) const;
  uint8_t encode_half_unit_(float value) const;
  float decode_half_unit_(uint8_t value) const;
  float median_(const float values[], uint8_t count) const;
  bool draw_display_(bool force_full_refresh);
  void draw_decimal_value_(GFXcanvas1 &black_canvas, GFXcanvas1 &red_canvas, float value,
                           int16_t text_y, const uint8_t *icon, int16_t fixed_icon_x);
  void draw_integer_value_(GFXcanvas1 &red_canvas, GFXcanvas1 &target, float value,
                           int16_t text_y, const uint8_t *icon, uint8_t text_width,
                           int16_t fixed_icon_x);
  void draw_display_content_(GFXcanvas1 &black_canvas, GFXcanvas1 &red_canvas,
                             float temperature, float humidity, float pressure, float static_iaq,
                             float battery, bool battery_valid);
  void capture_display_snapshot_();

  deep_sleep::DeepSleepComponent *deep_sleep_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *humidity_sensor_{nullptr};
  sensor::Sensor *pressure_sensor_{nullptr};
  sensor::Sensor *gas_resistance_sensor_{nullptr};
  sensor::Sensor *static_iaq_sensor_{nullptr};
  sensor::Sensor *adaptive_iaq_sensor_{nullptr};
  sensor::Sensor *iaq_accuracy_sensor_{nullptr};
  sensor::Sensor *co2_equivalent_sensor_{nullptr};
  sensor::Sensor *breath_voc_equivalent_sensor_{nullptr};
  sensor::Sensor *battery_sensor_{nullptr};
  sensor::Sensor *battery_raw_sensor_{nullptr};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_valid_sensor_{nullptr};
  binary_sensor::BinarySensor *bsec_stabilized_sensor_{nullptr};
  binary_sensor::BinarySensor *bsec_run_in_sensor_{nullptr};
  binary_sensor::BinarySensor *ota_maintenance_sensor_{nullptr};

  static EnviroSenseCube *active_bsec_instance_;
  SleepAwareBsec2 bsec_;
  PersistentLC709203F battery_gauge_;
  ESPPreferenceObject rtc_preference_;
  ESPPreferenceObject bsec_flash_preference_;
  ESPPreferenceObject bsec_flash_valid_preference_;
  ESPPreferenceObject bsec_clock_preference_;
  uint64_t bsec_clock_offset_ms_{0};
  bool bsec_clock_ready_{false};
  bool bsec_clock_failed_{false};
  SupportData support_data_{};

  RunState run_state_{STARTING};
  uint32_t state_deadline_{0};
  uint32_t bsec_cycle_started_at_{0};
  uint32_t bsec_measurement_started_at_{0};
  uint32_t api_authenticated_at_{0};
  uint32_t diagnostic_last_report_at_{0};
  bool diagnostic_logging_{false};
  bool bsec_filesystem_mounted_{false};
  bool cold_boot_{false};
  bool sensors_ready_{false};
  bool environment_sensor_ready_{false};
  bool bsec_session_active_{false};
  bool bsec_measurement_ready_{false};
  bool manual_display_refresh_requested_{false};
  bool api_client_authenticated_{false};

  float temperature_offset_{0.0f};
  float humidity_offset_{0.0f};
  float pressure_offset_{0.0f};
  float temperature_threshold_{0.0f};
  float humidity_threshold_{0.0f};
  float pressure_threshold_{0.0f};
  float static_iaq_threshold_{0.0f};
  float battery_threshold_{0.0f};

  float temperature_{NAN};
  float humidity_{NAN};
  float pressure_{NAN};
  float gas_resistance_{NAN};
  float static_iaq_{NAN};
  float adaptive_iaq_{NAN};
  float iaq_accuracy_{NAN};
  float co2_equivalent_{NAN};
  float breath_voc_equivalent_{NAN};
  float battery_voltage_{NAN};
  float battery_raw_{NAN};
  float battery_filtered_{NAN};
  bool bsec_stabilized_{false};
  bool bsec_run_in_complete_{false};
  bool battery_valid_{false};
};

}  // namespace envirosense_cube
}  // namespace esphome
