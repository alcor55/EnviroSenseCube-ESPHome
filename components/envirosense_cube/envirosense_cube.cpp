#include "envirosense_cube.h"

#include <SPI.h>
#include <LittleFS.h>
#include <Esp.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "E2154JS0C1_lib.h"
#include "icon_air_quality.h"
#include "icon_humidity.h"
#include "icon_pressure.h"
#include "icon_temperature.h"
#include "esphome/core/application.h"
#include "esphome/components/logger/logger.h"
#include "esphome/core/log.h"

extern "C" {
extern uint32_t _FS_start;
extern uint32_t _FS_end;
}

namespace esphome {
namespace envirosense_cube {

static const char *const TAG = "envirosense_cube";

static constexpr uint8_t SDA_PIN = 4;
static constexpr uint8_t SCL_PIN = 5;
static constexpr uint8_t BUSY_PIN = 12;
static constexpr uint8_t RST_PIN = 0;
static constexpr uint8_t DC_PIN = 15;
static constexpr uint8_t CS_PIN = 2;
static constexpr uint16_t DISPLAY_WIDTH = e2154js0c1::WIDTH;
static constexpr uint16_t DISPLAY_HEIGHT = e2154js0c1::HEIGHT;
static constexpr uint32_t DISPLAY_OPERATION_TIMEOUT_MS = 40000;

static constexpr uint32_t OTA_WINDOW_MS = 20000;
static constexpr uint32_t OTA_MAINTENANCE_START_DELAY_MS = 0;
static constexpr uint32_t WIFI_CONNECTION_TIMEOUT_MS = 15000;
static constexpr uint32_t API_CONNECTION_TIMEOUT_MS = 45000;
static constexpr uint32_t API_READY_DELAY_MS = 2000;
static constexpr uint32_t API_FLUSH_MS = 10000;
static constexpr uint32_t BSEC_MEASUREMENT_TIMEOUT_MS = 10000;
static constexpr uint32_t BSEC_CYCLE_INTERVAL_MS = 300000;
static constexpr uint32_t OTA_RECOVERY_RETRY_MS = 5000;
static constexpr uint32_t MINIMUM_DEEP_SLEEP_MS = 1000;
static constexpr const char *BSEC_BACKUP_PATH = "/bsec2-backup.bin";
static constexpr const char *BSEC_BACKUP_TEMP_PATH = "/bsec2-backup.tmp";
struct BsecBackupRecord {
  uint32_t magic;
  uint32_t state_hash;
  uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
};
static constexpr uint32_t BSEC_BACKUP_MAGIC = 0x42534332;

// Layout used by the immediately preceding firmware. It is retained only to
// migrate the live BSEC2 state after the one-time RTC format change.
struct LegacySupportData {
  int16_t publish_temperature_x10;
  uint16_t publish_humidity_x10;
  uint16_t publish_pressure_x2;
  uint16_t publish_static_iaq;
  uint16_t pressure_history[12];
  uint8_t publish_battery_x2;
  uint8_t temp_history[12];
  uint8_t hum_history[12];
  uint8_t static_iaq_history[12];
  uint8_t battery_history[8];
  uint8_t flags;
  uint8_t display_cycles_since_update;
  uint8_t cycles_since_publish_attempt;
  uint8_t display_update_count;
  uint8_t bsec_flash_cycles;
  uint8_t bsec_state[BSEC_MAX_STATE_BLOB_SIZE];
};
static_assert(sizeof(LegacySupportData) == 320, "Legacy RTC layout changed unexpectedly");

static const uint8_t BSEC_CONFIG_ULP[] PROGMEM = {
#include "config/bme688/bme688_sel_33v_300s_28d/bsec_selectivity.txt"
};
static_assert(sizeof(BSEC_CONFIG_ULP) == BSEC_MAX_PROPERTY_BLOB_SIZE,
              "The BSEC2 ULP configuration blob must have the size expected by the library");

static uint32_t bsec_state_hash(const uint8_t *state) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
    hash ^= state[i];
    hash *= 16777619UL;
  }
  return hash;
}

EnviroSenseCube *EnviroSenseCube::active_bsec_instance_ = nullptr;

bool PersistentLC709203F::begin_preserving_state(TwoWire *wire) {
  if (this->i2c_dev != nullptr)
    delete this->i2c_dev;
  this->i2c_dev = new Adafruit_I2CDevice(LC709203F_I2CADDR_DEFAULT, wire);
  return this->i2c_dev != nullptr && this->i2c_dev->begin();
}

bool PersistentLC709203F::read_cell(float &voltage, float &percent) {
  uint16_t raw_voltage;
  uint16_t raw_percent;
  if (!this->readWord(LC709203F_CMD_CELLVOLTAGE, &raw_voltage) ||
      !this->readWord(LC709203F_CMD_CELLITE, &raw_percent))
    return false;
  voltage = raw_voltage / 1000.0f;
  percent = raw_percent / 10.0f;
  return true;
}

bool PersistentLC709203F::set_cell_temperature(float temperature_c) {
  temperature_c = std::max(-20.0f, std::min(60.0f, temperature_c));
  const uint16_t temperature_kelvin_x10 =
      static_cast<uint16_t>(roundf((temperature_c + 273.15f) * 10.0f));
  return this->writeWord(LC709203F_CMD_CELLTEMPERATURE, temperature_kelvin_x10);
}

EnviroSenseCube::EnviroSenseCube() = default;

float EnviroSenseCube::get_setup_priority() const { return setup_priority::LATE + 1.0f; }

void EnviroSenseCube::setup() {
  this->rtc_preference_ = global_preferences->make_preference<SupportData>(RTC_STATE_KEY, false);
  this->bsec_flash_preference_ =
      global_preferences->make_preference<uint8_t[BSEC_MAX_STATE_BLOB_SIZE]>(BSEC_FLASH_STATE_KEY, true);
  this->bsec_flash_valid_preference_ =
      global_preferences->make_preference<uint8_t>(BSEC_FLASH_VALID_KEY, true);
  this->cold_boot_ = !this->rtc_preference_.load(&this->support_data_);

  if (this->cold_boot_) {
    if (this->migrate_legacy_support_data_()) {
      this->cold_boot_ = false;
      this->save_support_data_();
      ESP_LOGI(TAG, "Migrated legacy 320-byte RTC state to %u bytes; BSEC2 state preserved",
               static_cast<unsigned>(sizeof(SupportData)));
    } else {
      this->initialize_support_data_();
    }
  }
  // Allocate after the existing records: their offsets and calibration remain unchanged.
  this->bsec_clock_preference_ =
      global_preferences->make_preference<BsecClockState>(BSEC_CLOCK_RTC_KEY, false);
  BsecClockState clock{};
  const bool deep_sleep_wake = ESP.getResetInfoPtr()->reason == REASON_DEEP_SLEEP_AWAKE;
  const bool matching_clock = !this->cold_boot_ && this->flag_(FLAG_BSEC_STATE_VALID) &&
      this->bsec_clock_preference_.load(&clock) &&
      bsec_clock_matches(clock, bsec_state_hash(this->support_data_.bsec_state));
  if (matching_clock)
    this->bsec_clock_offset_ms_ = bsec_clock_resume(clock, deep_sleep_wake);
  this->bsec_clock_ready_ = true;
  ESP_LOGW(TAG, "BSEC2 clock: %s base=%llums deep_sleep_wake=%s; calibration unchanged",
           matching_clock ? "restored from RTC" : "new time reference",
           static_cast<unsigned long long>(this->bsec_clock_offset_ms_), YESNO(deep_sleep_wake));
  this->ensure_bsec_backup_();
  ESP_LOGW(TAG, "Boot diagnostic: reset=%s heap=%u max_block=%u fragmentation=%u%%",
           ESP.getResetReason().c_str(), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(),
           ESP.getHeapFragmentation());

  if (this->flag_(FLAG_OTA_MAINTENANCE_REQUESTED)) {
    ESP_LOGI(TAG, "Persistent OTA maintenance mode active; keeping WiFi and device awake");
    if (this->deep_sleep_ != nullptr)
      this->deep_sleep_->prevent_deep_sleep();
    if (wifi::global_wifi_component != nullptr)
      wifi::global_wifi_component->enable();
  }
  if (this->ota_maintenance_sensor_ != nullptr)
    this->ota_maintenance_sensor_->publish_state(this->flag_(FLAG_OTA_MAINTENANCE_REQUESTED));

  ESP_LOGI(TAG, "Boot mode: %s; RTC state size: %u bytes; environment cycle: BSEC2/gas",
           this->cold_boot_ ? "cold" : "deep-sleep wake", static_cast<unsigned>(sizeof(SupportData)));

  Wire.begin(SDA_PIN, SCL_PIN);

  if (this->cold_boot_) {
    ESP_LOGI(TAG, "Cold boot: enabling the emergency OTA/API window");
    wifi::global_wifi_component->enable();
    this->state_deadline_ = millis() + OTA_WINDOW_MS;
    this->run_state_ = OTA_WINDOW;
  } else {
    this->start_cycle_();
  }
}

void EnviroSenseCube::loop() {
  const uint32_t now = millis();
  if (this->diagnostic_logging_ && now - this->diagnostic_last_report_at_ >= 30000) {
    this->diagnostic_last_report_at_ = now;
    ESP_LOGI(TAG, "Alive: uptime=%lus state=%u OTA=%s HA=%s WiFi=%s heap=%u max_block=%u fragmentation=%u%%",
             now / 1000, this->run_state_, YESNO(this->flag_(FLAG_OTA_MAINTENANCE_REQUESTED)),
             YESNO(this->api_client_authenticated_), YESNO(wifi::global_wifi_component->is_connected()),
             ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(), ESP.getHeapFragmentation());
  }
  if (this->manual_display_refresh_requested_ &&
      (this->run_state_ == FINISHED || this->run_state_ == WAITING_FOR_NEXT_CYCLE) &&
      std::isfinite(this->temperature_) && std::isfinite(this->humidity_) &&
      std::isfinite(this->pressure_)) {
    ESP_LOGI(TAG, "Manual display refresh starting");
    if (this->draw_display_(true)) {
      this->manual_display_refresh_requested_ = false;
      this->set_flag_(FLAG_DISPLAY_UPDATE_PENDING, false);
      this->support_data_.display_cycles_since_update = 0;
      this->save_support_data_();
      ESP_LOGI(TAG, "Manual display refresh completed and saved");
    } else {
      this->manual_display_refresh_requested_ = false;
      ESP_LOGW(TAG, "Manual display refresh failed");
    }
  }
  switch (this->run_state_) {
    case OTA_WINDOW:
      if (static_cast<int32_t>(now - this->state_deadline_) >= 0)
        this->start_cycle_();
      break;

    case WAITING_FOR_BSEC:
      if (!this->bsec_.run()) {
        ESP_LOGW(TAG, "BSEC2 run failed while waiting: BSEC=%d BME68x=%d", this->bsec_.status,
                 this->bsec_.sensor.status);
        this->abort_cycle_();
      } else if (this->bsec_measurement_ready_) {
        ESP_LOGI(TAG, "BSEC2 gas sample ready after %u ms", now - this->bsec_measurement_started_at_);
        this->capture_bsec_state_();
        this->checkpoint_bsec_state_();
        this->complete_measurement_cycle_();
      } else if (static_cast<int32_t>(now - this->state_deadline_) >= 0) {
        ESP_LOGW(TAG, "BSEC2 gas measurement timed out after %u ms: BSEC=%d BME68x=%d",
                 BSEC_MEASUREMENT_TIMEOUT_MS, this->bsec_.status, this->bsec_.sensor.status);
        this->abort_cycle_();
      }
      break;

    case WAITING_FOR_WIFI:
      if (wifi::global_wifi_component != nullptr && wifi::global_wifi_component->is_connected()) {
        ESP_LOGI(TAG, "WiFi connected; allowing up to 45 seconds for Native API authentication");
        this->state_deadline_ = now + API_CONNECTION_TIMEOUT_MS;
        this->run_state_ = WAITING_FOR_API;
      } else if (static_cast<int32_t>(now - this->state_deadline_) >= 0) {
        ESP_LOGW(TAG, "WiFi connection timed out; no retry on the next wake");
        this->finish_cycle_();
      }
      break;

    case WAITING_FOR_API:
      if (this->api_client_authenticated_ && api::global_api_server != nullptr &&
          api::global_api_server->is_connected()) {
        if (now - this->api_authenticated_at_ >= API_READY_DELAY_MS) {
          ESP_LOGI(TAG, "Home Assistant authenticated and settled; publishing all available states");
          this->publish_states_();
          this->state_deadline_ = now + API_FLUSH_MS;
          this->run_state_ = WAITING_FOR_API_FLUSH;
        }
      } else {
        this->api_client_authenticated_ = false;
      }
      if (this->run_state_ == WAITING_FOR_API &&
          static_cast<int32_t>(now - this->state_deadline_) >= 0) {
        ESP_LOGW(TAG, "Native API authentication timed out; no retry on the next wake");
        this->finish_cycle_();
      }
      break;

    case WAITING_FOR_API_FLUSH:
      if (static_cast<int32_t>(now - this->state_deadline_) >= 0) {
        ESP_LOGI(TAG, "API flush interval complete");
        this->finish_cycle_();
      }
      break;

    case WAITING_FOR_NEXT_CYCLE:
      if (!this->flag_(FLAG_OTA_MAINTENANCE_REQUESTED) && !this->bsec_clock_failed_) {
        ESP_LOGI(TAG, "OTA maintenance disabled; entering normal deep sleep");
        this->enter_sleep_();
      } else if (static_cast<int32_t>(now - this->state_deadline_) >= 0) {
        if (this->bsec_session_active_) {
          ESP_LOGI(TAG, "OTA maintenance interval complete; continuing live BSEC2 session");
          this->start_continuous_bsec_cycle_();
        } else {
          ESP_LOGI(TAG, "OTA maintenance retry interval complete; reinitializing BSEC2 session");
          this->start_cycle_();
        }
      }
      break;

    default:
      break;
  }
}

void EnviroSenseCube::notify_api_client_connected(const std::string &client_info,
                                                  const std::string &client_address) {
  if (client_info.compare(0, 14, "Home Assistant") != 0) {
    ESP_LOGI(TAG, "API client connected (not HA): '%s' from %s", client_info.c_str(), client_address.c_str());
    return;
  }
  this->api_client_authenticated_ = true;
  this->api_authenticated_at_ = millis();
  ESP_LOGI(TAG, "Authenticated API client connected: '%s' from %s", client_info.c_str(), client_address.c_str());
  // Republish finite states when HA reconnects, even between measurement cycles.
  if (this->bsec_measurement_ready_)
    this->set_timeout("ha_republish", API_READY_DELAY_MS, [this]() {
      if (this->api_client_authenticated_)
        this->publish_states_();
    });
}

void EnviroSenseCube::notify_api_client_disconnected(const std::string &client_info,
                                                     const std::string &client_address) {
  if (client_info.compare(0, 14, "Home Assistant") == 0) {
    this->api_client_authenticated_ = false;
    this->cancel_timeout("ha_republish");
    ESP_LOGW(TAG, "Home Assistant API disconnected: %s heap=%u max_block=%u",
             client_address.c_str(), ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
  }
}

void EnviroSenseCube::set_diagnostic_logging(bool enabled) {
  this->diagnostic_logging_ = enabled;
  if (logger::global_logger != nullptr) {
    const uint8_t level = enabled ? ESPHOME_LOG_LEVEL_INFO : ESPHOME_LOG_LEVEL_WARN;
    logger::global_logger->set_log_level(level);
    for (const char *tag : {TAG, "envirosense_epaper", "api", "api.connection", "wifi"})
      logger::global_logger->set_log_level(tag, level);
  }
  ESP_LOGW(TAG, "Diagnostic logging %s", enabled ? "enabled" : "disabled (warnings/errors only)");
}

void EnviroSenseCube::set_flag_(SupportFlag flag, bool enabled) {
  if (enabled)
    this->support_data_.flags |= flag;
  else
    this->support_data_.flags &= ~flag;
}

void EnviroSenseCube::set_ota_maintenance(bool enabled) {
  const bool current = this->flag_(FLAG_OTA_MAINTENANCE_REQUESTED);
  this->set_flag_(FLAG_OTA_MAINTENANCE_REQUESTED, enabled);

  if (enabled) {
    if (this->deep_sleep_ != nullptr)
      this->deep_sleep_->prevent_deep_sleep();
    if (wifi::global_wifi_component != nullptr)
      wifi::global_wifi_component->enable();
    if (this->run_state_ == OTA_WINDOW) {
      const uint32_t expedited_deadline = millis() + OTA_MAINTENANCE_START_DELAY_MS;
      if (static_cast<int32_t>(this->state_deadline_ - expedited_deadline) > 0) {
        this->state_deadline_ = expedited_deadline;
        ESP_LOGI(TAG, "OTA maintenance enabled; sensor start expedited to %u ms",
                 OTA_MAINTENANCE_START_DELAY_MS);
      }
    }
  } else if (this->deep_sleep_ != nullptr) {
    this->deep_sleep_->allow_deep_sleep();
  }

  if (this->ota_maintenance_sensor_ != nullptr)
    this->ota_maintenance_sensor_->publish_state(enabled);

  if (current != enabled) {
    this->save_support_data_();
    ESP_LOGI(TAG, "OTA maintenance mode changed to %s and saved in RTC memory", enabled ? "ON" : "OFF");
  } else {
    ESP_LOGD(TAG, "OTA maintenance mode confirmed %s", enabled ? "ON" : "OFF");
  }
}

void EnviroSenseCube::request_display_refresh() {
  this->manual_display_refresh_requested_ = true;
  this->set_flag_(FLAG_DISPLAY_UPDATE_PENDING, true);
  ESP_LOGI(TAG, "Manual display refresh requested");
}

void EnviroSenseCube::save_bsec_state_to_flash() {
  if (!this->flag_(FLAG_BSEC_STATE_VALID)) {
    ESP_LOGW(TAG, "Manual BSEC2 flash checkpoint rejected: no valid state in RTC memory");
    return;
  }

  const uint8_t flash_state_valid = BSEC_FLASH_STATE_VALID;
  if (!this->bsec_flash_preference_.save(&this->support_data_.bsec_state) ||
      !this->bsec_flash_valid_preference_.save(&flash_state_valid)) {
    ESP_LOGW(TAG, "Manual BSEC2 flash checkpoint failed");
    return;
  }
  if (!global_preferences->sync()) {
    ESP_LOGW(TAG, "Manual BSEC2 flash checkpoint could not be committed");
    return;
  }

  this->support_data_.bsec_flash_cycles = 0;
  this->set_flag_(FLAG_BSEC_QUALITY_CHECKPOINT_SAVED,
                  std::isfinite(this->iaq_accuracy_) && this->iaq_accuracy_ >= 2.0f);
  this->save_support_data_();
  ESP_LOGI(TAG, "Manual BSEC2 flash checkpoint saved: hash=%08lX accuracy=%.0f",
           static_cast<unsigned long>(bsec_state_hash(this->support_data_.bsec_state)),
           this->iaq_accuracy_);
}

void EnviroSenseCube::reset_bsec_state() {
  ESP_LOGW(TAG, "Manual BSEC2 calibration reset requested");

  const uint8_t flash_state_valid = BSEC_FLASH_STATE_INVALID;
  if (!this->bsec_flash_valid_preference_.save(&flash_state_valid) ||
      !global_preferences->sync()) {
    ESP_LOGE(TAG, "BSEC2 calibration reset aborted: flash invalidation failed");
    return;
  }

  memset(this->support_data_.bsec_state, 0, sizeof(this->support_data_.bsec_state));
  this->set_flag_(FLAG_BSEC_STATE_VALID, false);
  this->set_flag_(FLAG_BSEC_QUALITY_CHECKPOINT_SAVED, false);
  this->support_data_.bsec_flash_cycles = BSEC_FLASH_NOT_SAVED;
  this->iaq_accuracy_ = NAN;
  this->bsec_stabilized_ = false;
  this->bsec_run_in_complete_ = false;
  this->save_support_data_();

  ESP_LOGW(TAG,
           "BSEC2 calibration reset complete: RTC state cleared, flash checkpoint invalidated; rebooting");
  App.safe_reboot();
}

void EnviroSenseCube::restore_bsec_backup() {
  uint8_t backup_state[BSEC_MAX_STATE_BLOB_SIZE];
  if (!this->read_bsec_backup_(backup_state)) {
    ESP_LOGE(TAG, "BSEC2 backup restore rejected: no valid backup is available");
    return;
  }

  const uint8_t flash_state_valid = BSEC_FLASH_STATE_VALID;
  if (!this->bsec_flash_preference_.save(&backup_state) ||
      !this->bsec_flash_valid_preference_.save(&flash_state_valid) ||
      !global_preferences->sync()) {
    ESP_LOGE(TAG, "BSEC2 backup restore aborted: flash checkpoint write failed");
    return;
  }

  memcpy(this->support_data_.bsec_state, backup_state, sizeof(backup_state));
  this->set_flag_(FLAG_BSEC_STATE_VALID, true);
  this->set_flag_(FLAG_BSEC_QUALITY_CHECKPOINT_SAVED, false);
  this->support_data_.bsec_flash_cycles = 0;
  this->save_support_data_();
  ESP_LOGW(TAG, "BSEC2 backup restored: hash=%08lX; rebooting",
           static_cast<unsigned long>(bsec_state_hash(backup_state)));
  App.safe_reboot();
}

void EnviroSenseCube::dump_config() {
  ESP_LOGCONFIG(TAG,
                "EnviroSenseCube:\n"
                "  Sampling/deep sleep: 300 seconds\n"
                "  BSEC2 static/adaptive IAQ: ULP every cycle (5 minutes)\n"
                "  OTA maintenance: one continuous BSEC2 session across cycles\n"
                "  BSEC2 cadence: 300 seconds measured from cycle start\n"
                "  BSEC2 state: RTC each cycle; flash at accuracy >=2, then every 6 hours\n"
                "  BSEC2 safety backup: verified LittleFS file, captured once\n"
                "  Emergency OTA/API window after cold boot: 20 seconds\n"
                "  OTA-maintenance sensor start delay: none\n"
                "  Diagnostic stay-awake mode: disabled\n"
                "  Heartbeat: 6 cycles (30 minutes)\n"
                "  Display: E2154JS0C1 152x152 black/white/red; global update only\n"
                "  Display minimum interval: 6 cycles (30 minutes)\n"
                "  Display forced refresh: 72 cycles (6 hours)\n"
                "  Display operation timeout: 40 seconds\n"
                "  WiFi/API timeouts: 15/45 seconds\n"
                "  Offsets T/H/P: %.1f C / %.1f %% / %.1f hPa\n"
                "  Publish thresholds T/H/P/IAQ/battery: %.1f / %.1f / %.1f / %.1f / %.1f\n"
                "  BME688 address: 0x77\n"
                "  LC709203F address: 0x0B",
                this->temperature_offset_, this->humidity_offset_, this->pressure_offset_,
                this->temperature_threshold_, this->humidity_threshold_, this->pressure_threshold_,
                this->static_iaq_threshold_, this->battery_threshold_);
}

void EnviroSenseCube::initialize_support_data_() {
  memset(&this->support_data_, 0, sizeof(this->support_data_));
  this->support_data_.display_cycles_since_update = DISPLAY_MIN_CYCLES;
  this->support_data_.cycles_since_publish_attempt = HEARTBEAT_CYCLES;
  this->support_data_.bsec_flash_cycles = BSEC_FLASH_NOT_SAVED;
  this->support_data_.display_update_count = DISPLAY_FULL_REFRESH_CYCLES;
}

bool EnviroSenseCube::migrate_legacy_support_data_() {
  static constexpr uint8_t LEGACY_DATA_WORDS = sizeof(LegacySupportData) / sizeof(uint32_t);
  static constexpr uint8_t LEGACY_TOTAL_WORDS = LEGACY_DATA_WORDS + 1;
  uint32_t words[LEGACY_TOTAL_WORDS];

  // ESPHome keeps ordinary RTC preferences in user-memory words 32..127.
  // Scan the possible start offsets so this remains robust if another small
  // framework preference was allocated before this component.
  for (uint8_t offset = 32; offset + LEGACY_TOTAL_WORDS <= 128; offset++) {
    if (!ESP.rtcUserMemoryRead(offset, words, sizeof(words)))
      continue;
    uint32_t crc = RTC_STATE_KEY;
    for (uint8_t i = 0; i < LEGACY_DATA_WORDS; i++)
      crc ^= (words[i] * 2654435769UL) >> 1;
    if (crc != words[LEGACY_DATA_WORDS])
      continue;

    LegacySupportData legacy{};
    memcpy(&legacy, words, sizeof(legacy));
    this->initialize_support_data_();
    this->support_data_.publish_temperature_x10 = legacy.publish_temperature_x10;
    this->support_data_.publish_humidity_x10 = legacy.publish_humidity_x10;
    this->support_data_.publish_pressure_x2 = legacy.publish_pressure_x2;
    this->support_data_.publish_static_iaq = legacy.publish_static_iaq;
    this->support_data_.publish_battery_x2 = legacy.publish_battery_x2;
    memcpy(this->support_data_.battery_history, legacy.battery_history,
           sizeof(this->support_data_.battery_history));
    this->support_data_.flags = legacy.flags;
    this->support_data_.display_cycles_since_update = DISPLAY_MIN_CYCLES;
    this->support_data_.cycles_since_publish_attempt = legacy.cycles_since_publish_attempt;
    this->support_data_.display_update_count = DISPLAY_FULL_REFRESH_CYCLES;
    this->support_data_.bsec_flash_cycles = legacy.bsec_flash_cycles;
    memcpy(this->support_data_.bsec_state, legacy.bsec_state, sizeof(this->support_data_.bsec_state));
    if ((legacy.flags & FLAG_LAST_STATIC_IAQ_VALID) != 0)
      this->support_data_.last_static_iaq_x2 = legacy.static_iaq_history[11];
    this->support_data_.display_snapshot.flags = 0;
    this->set_flag_(FLAG_DISPLAY_UPDATE_PENDING, true);
    return true;
  }
  return false;
}

void EnviroSenseCube::save_support_data_() {
  if (!this->rtc_preference_.save(&this->support_data_)) {
    ESP_LOGW(TAG, "Unable to save state in RTC memory");
    return;
  }
  if (this->bsec_clock_ready_)
    this->save_bsec_clock_();
}

bool EnviroSenseCube::save_bsec_clock_(uint32_t planned_sleep_ms) {
  BsecClockState clock{};
  clock.elapsed_ms = this->bsec_session_active_
                         ? static_cast<uint64_t>(this->bsec_.getTimeMs())
                         : this->bsec_clock_offset_ms_ + millis();
  clock.planned_sleep_ms = planned_sleep_ms;
  clock.state_hash = bsec_state_hash(this->support_data_.bsec_state);
  clock.version = BSEC_CLOCK_VERSION;
  if (!this->bsec_clock_preference_.save(&clock)) {
    this->bsec_clock_failed_ = true;
    ESP_LOGW(TAG, "Unable to save BSEC2 clock in RTC; deep sleep is unsafe");
    return false;
  }
  this->bsec_clock_failed_ = false;
  ESP_LOGI(TAG, "BSEC2 clock saved: now=%llums sleep=%ums next_boot_base=%llums hash=%08lX",
           static_cast<unsigned long long>(clock.elapsed_ms), planned_sleep_ms,
           static_cast<unsigned long long>(clock.elapsed_ms + planned_sleep_ms),
           static_cast<unsigned long>(clock.state_hash));
  return true;
}

bool EnviroSenseCube::mount_bsec_filesystem_() {
  if (this->bsec_filesystem_mounted_)
    return true;
  LittleFSConfig config;
  config.setAutoFormat(false);
  LittleFS.setConfig(config);
  this->bsec_filesystem_mounted_ = LittleFS.begin();
  if (!this->bsec_filesystem_mounted_) {
    // Format only the explicitly reserved area, and only when every byte is erased.
    const uint32_t start = reinterpret_cast<uintptr_t>(&_FS_start) - 0x40200000;
    const uint32_t end = reinterpret_cast<uintptr_t>(&_FS_end) - 0x40200000;
    bool blank = end > start && end - start == 32768;
    uint32_t words[64];
    for (uint32_t address = start; blank && address < end; address += sizeof(words)) {
      if (!ESP.flashRead(address, words, sizeof(words))) {
        blank = false;
        break;
      }
      for (uint32_t word : words)
        if (word != 0xFFFFFFFFUL) {
          blank = false;
          break;
        }
      yield();
    }
    if (blank) {
      ESP_LOGW(TAG, "Initializing verified-empty 32 KiB BSEC2 backup filesystem");
      this->bsec_filesystem_mounted_ = LittleFS.format() && LittleFS.begin();
    }
    if (!this->bsec_filesystem_mounted_)
      ESP_LOGE(TAG, "BSEC2 backup filesystem unavailable; existing data will not be formatted");
  }
  return this->bsec_filesystem_mounted_;
}

bool EnviroSenseCube::read_bsec_backup_(uint8_t *state) {
  if (!this->mount_bsec_filesystem_())
    return false;
  auto file = LittleFS.open(BSEC_BACKUP_PATH, "r");
  BsecBackupRecord record{};
  if (!file || file.size() != sizeof(record) ||
      file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) != sizeof(record))
    return false;
  file.close();
  if (record.magic != BSEC_BACKUP_MAGIC || record.state_hash != bsec_state_hash(record.state))
    return false;
  memcpy(state, record.state, BSEC_MAX_STATE_BLOB_SIZE);
  return true;
}

void EnviroSenseCube::ensure_bsec_backup_() {
  if (!this->mount_bsec_filesystem_())
    return;
  uint8_t existing_state[BSEC_MAX_STATE_BLOB_SIZE];
  if (LittleFS.exists(BSEC_BACKUP_PATH)) {
    if (this->read_bsec_backup_(existing_state))
      ESP_LOGI(TAG, "BSEC2 safety backup verified: hash=%08lX",
               static_cast<unsigned long>(bsec_state_hash(existing_state)));
    else
      ESP_LOGE(TAG, "Existing BSEC2 backup is invalid; preserved for recovery");
    return;
  }
  if (!this->flag_(FLAG_BSEC_STATE_VALID)) {
    ESP_LOGW(TAG, "BSEC2 safety backup deferred: no valid RTC state is available");
    return;
  }

  BsecBackupRecord record{};
  record.magic = BSEC_BACKUP_MAGIC;
  memcpy(record.state, this->support_data_.bsec_state, sizeof(record.state));
  record.state_hash = bsec_state_hash(record.state);
  auto file = LittleFS.open(BSEC_BACKUP_TEMP_PATH, "w");
  if (!file) {
    ESP_LOGE(TAG, "Unable to create BSEC2 safety backup");
    return;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  file.flush();
  file.close();
  if (written != sizeof(record) || !LittleFS.rename(BSEC_BACKUP_TEMP_PATH, BSEC_BACKUP_PATH) ||
      !this->read_bsec_backup_(existing_state)) {
    ESP_LOGE(TAG, "BSEC2 safety backup write/verification failed");
    return;
  }
  ESP_LOGW(TAG, "BSEC2 safety backup created and verified: hash=%08lX",
           static_cast<unsigned long>(record.state_hash));
}

bool EnviroSenseCube::initialize_sensors_() {
  this->environment_sensor_ready_ = this->initialize_bsec_();
  this->bsec_session_active_ = this->environment_sensor_ready_;
  if (!this->environment_sensor_ready_)
    ESP_LOGE(TAG, "BME688 initialization failed");

  bool gauge_ready = this->cold_boot_ ? this->battery_gauge_.begin(&Wire)
                                      : this->battery_gauge_.begin_preserving_state(&Wire);
  if (!gauge_ready) {
    ESP_LOGE(TAG, "LC709203F not found");
  } else {
    ESP_LOGI(TAG, "LC709203F initialized at 0x0B using %s path",
             this->cold_boot_ ? "cold-boot" : "state-preserving");
    this->battery_gauge_.setPowerMode(LC709203F_POWER_OPERATE);
    this->battery_gauge_.setPackAPA(0x2A);
    this->battery_gauge_.setTemperatureMode(LC709203F_TEMPERATURE_I2C);
    if (this->cold_boot_)
      this->battery_gauge_.set_cell_temperature(25.0f);
    this->battery_gauge_.setAlarmVoltage(0);
  }
  return this->environment_sensor_ready_ && gauge_ready;
}

bool EnviroSenseCube::initialize_bsec_() {
  active_bsec_instance_ = this;
  this->bsec_.setClockOffsetMs(this->bsec_clock_offset_ms_);
  ESP_LOGI(TAG, "Starting BSEC2/BME688 initialization; free heap: %u bytes", ESP.getFreeHeap());
  if (!this->bsec_.begin(0x77, Wire)) {
    ESP_LOGE(TAG, "BSEC2/BME688 begin failed: BSEC=%d BME68x=%d", this->bsec_.status,
             this->bsec_.sensor.status);
    return false;
  }

  const size_t selected_config_size = BSEC_MAX_PROPERTY_BLOB_SIZE;
  ESP_LOGI(TAG, "BSEC2 begin completed; copying %u-byte ULP configuration from flash to RAM",
           static_cast<unsigned>(selected_config_size));
  auto *ram_config = new (std::nothrow) uint8_t[selected_config_size];
  if (ram_config == nullptr) {
    ESP_LOGE(TAG, "Unable to allocate BSEC2 configuration buffer; free heap: %u bytes", ESP.getFreeHeap());
    return false;
  }
  memcpy_P(ram_config, BSEC_CONFIG_ULP, selected_config_size);

  const bool config_ok = this->bsec_.setConfig(ram_config);
  delete[] ram_config;
  ESP_LOGI(TAG, "BSEC2 configuration call completed; free heap: %u bytes", ESP.getFreeHeap());
  if (!config_ok) {
    ESP_LOGE(TAG, "BSEC2 configuration failed: status=%d", this->bsec_.status);
    return false;
  }

  this->restore_bsec_state_();

  bsecSensor sensor_list[] = {
      BSEC_OUTPUT_IAQ,
      BSEC_OUTPUT_STATIC_IAQ,
      BSEC_OUTPUT_RAW_TEMPERATURE,
      BSEC_OUTPUT_RAW_PRESSURE,
      BSEC_OUTPUT_RAW_HUMIDITY,
      BSEC_OUTPUT_RAW_GAS,
      BSEC_OUTPUT_STABILIZATION_STATUS,
      BSEC_OUTPUT_RUN_IN_STATUS,
      BSEC_OUTPUT_CO2_EQUIVALENT,
      BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
  };
  if (!this->bsec_.updateSubscription(sensor_list, sizeof(sensor_list) / sizeof(sensor_list[0]),
                                      BSEC_SAMPLE_RATE_ULP)) {
    ESP_LOGE(TAG, "BSEC2 subscription failed: status=%d", this->bsec_.status);
    return false;
  }
  this->bsec_.setTemperatureOffset(-this->temperature_offset_);
  this->bsec_.attachCallback(bsec_data_callback_);
  ESP_LOGI(TAG, "BSEC2 %u.%u.%u.%u initialized in ULP mode", this->bsec_.version.major,
           this->bsec_.version.minor, this->bsec_.version.major_bugfix, this->bsec_.version.minor_bugfix);
  return true;
}

bool EnviroSenseCube::restore_bsec_state_() {
  if (this->flag_(FLAG_BSEC_STATE_VALID)) {
    if (this->bsec_.setState(this->support_data_.bsec_state)) {
      ESP_LOGI(TAG, "BSEC2 state restored from RTC memory: hash=%08lX",
               static_cast<unsigned long>(bsec_state_hash(this->support_data_.bsec_state)));
      return true;
    }
    ESP_LOGW(TAG, "BSEC2 RTC state rejected: status=%d", this->bsec_.status);
    this->set_flag_(FLAG_BSEC_STATE_VALID, false);
  }

  uint8_t flash_state_valid = BSEC_FLASH_STATE_VALID;
  const bool has_flash_valid_marker = this->bsec_flash_valid_preference_.load(&flash_state_valid);
  const bool flash_checkpoint_enabled =
      !has_flash_valid_marker || flash_state_valid == BSEC_FLASH_STATE_VALID;
  if (!flash_checkpoint_enabled) {
    ESP_LOGI(TAG, "BSEC2 flash checkpoint is explicitly invalidated");
  }

  uint8_t flash_state[BSEC_MAX_STATE_BLOB_SIZE];
  if (flash_checkpoint_enabled && this->bsec_flash_preference_.load(&flash_state) &&
      this->bsec_.setState(flash_state)) {
    // The flash checkpoint has no RTC timestamp. Never pair it with a stale clock.
    this->bsec_clock_offset_ms_ = 0;
    this->bsec_.setClockOffsetMs(0);
    memcpy(this->support_data_.bsec_state, flash_state, sizeof(flash_state));
    this->set_flag_(FLAG_BSEC_STATE_VALID, true);
    this->support_data_.bsec_flash_cycles = 0;
    this->ensure_bsec_backup_();
    ESP_LOGI(TAG, "BSEC2 state restored from flash checkpoint");
    return true;
  }

  ESP_LOGI(TAG, "BSEC2 starts without a saved calibration state");
  this->bsec_clock_offset_ms_ = 0;
  this->bsec_.setClockOffsetMs(0);
  return false;
}

void EnviroSenseCube::capture_bsec_state_() {
  uint8_t previous_state[BSEC_MAX_STATE_BLOB_SIZE];
  memcpy(previous_state, this->support_data_.bsec_state, sizeof(previous_state));
  if (!this->bsec_.getState(this->support_data_.bsec_state)) {
    ESP_LOGW(TAG, "Unable to capture BSEC2 state: status=%d", this->bsec_.status);
    return;
  }
  uint16_t changed_bytes = 0;
  for (size_t i = 0; i < sizeof(previous_state); i++) {
    if (previous_state[i] != this->support_data_.bsec_state[i])
      changed_bytes++;
  }
  this->set_flag_(FLAG_BSEC_STATE_VALID, true);
  ESP_LOGI(TAG,
           "BSEC2 state captured: old_hash=%08lX new_hash=%08lX changed_bytes=%u accuracy=%.0f "
           "stabilized=%s run_in=%s",
           static_cast<unsigned long>(bsec_state_hash(previous_state)),
           static_cast<unsigned long>(bsec_state_hash(this->support_data_.bsec_state)), changed_bytes,
           this->iaq_accuracy_, YESNO(this->bsec_stabilized_), YESNO(this->bsec_run_in_complete_));
}

void EnviroSenseCube::checkpoint_bsec_state_() {
  if (!this->flag_(FLAG_BSEC_STATE_VALID))
    return;

  if (this->support_data_.bsec_flash_cycles != BSEC_FLASH_NOT_SAVED &&
      this->support_data_.bsec_flash_cycles < BSEC_FLASH_CHECKPOINT_CYCLES)
    this->support_data_.bsec_flash_cycles++;

  const bool checkpoint_due =
      !this->flag_(FLAG_BSEC_QUALITY_CHECKPOINT_SAVED) ||
      this->support_data_.bsec_flash_cycles == BSEC_FLASH_NOT_SAVED ||
      this->support_data_.bsec_flash_cycles >= BSEC_FLASH_CHECKPOINT_CYCLES;
  if (!checkpoint_due || !std::isfinite(this->iaq_accuracy_) || this->iaq_accuracy_ < 2.0f)
    return;

  const uint8_t flash_state_valid = BSEC_FLASH_STATE_VALID;
  if (this->bsec_flash_preference_.save(&this->support_data_.bsec_state) &&
      this->bsec_flash_valid_preference_.save(&flash_state_valid) && global_preferences->sync()) {
    this->support_data_.bsec_flash_cycles = 0;
    this->set_flag_(FLAG_BSEC_QUALITY_CHECKPOINT_SAVED, true);
    ESP_LOGW(TAG, "BSEC2 state checkpointed to flash: accuracy=%.0f hash=%08lX",
             this->iaq_accuracy_, static_cast<unsigned long>(bsec_state_hash(this->support_data_.bsec_state)));
  } else {
    ESP_LOGW(TAG, "Unable to checkpoint BSEC2 state to flash");
  }
}

void EnviroSenseCube::reset_cycle_measurements_() {
  this->temperature_ = NAN;
  this->humidity_ = NAN;
  this->pressure_ = NAN;
  this->gas_resistance_ = NAN;
  this->static_iaq_ = NAN;
  this->adaptive_iaq_ = NAN;
  this->iaq_accuracy_ = NAN;
  this->co2_equivalent_ = NAN;
  this->breath_voc_equivalent_ = NAN;
  this->bsec_stabilized_ = false;
  this->bsec_run_in_complete_ = false;
  this->bsec_measurement_ready_ = false;
}

void EnviroSenseCube::start_cycle_() {
  this->reset_cycle_measurements_();
  this->sensors_ready_ = this->initialize_sensors_();
  ESP_LOGI(TAG, "Sensor initialization complete: %s", this->sensors_ready_ ? "ready" : "failed");
  if (!this->sensors_ready_) {
    ESP_LOGW(TAG, "Cycle aborted; retrying after normal deep sleep");
    this->abort_cycle_();
    return;
  }

  this->read_battery_();
  this->bsec_cycle_started_at_ = millis();
  if (!this->acquire_bsec_measurement_()) {
    ESP_LOGW(TAG, "Cycle aborted; retrying after normal deep sleep");
    this->abort_cycle_();
    return;
  }
  if (!this->bsec_measurement_ready_) {
    this->bsec_measurement_started_at_ = millis();
    this->state_deadline_ = this->bsec_measurement_started_at_ + BSEC_MEASUREMENT_TIMEOUT_MS;
    this->run_state_ = WAITING_FOR_BSEC;
    ESP_LOGI(TAG, "BSEC2 measurement started asynchronously; waiting up to %u ms for gas data",
             BSEC_MEASUREMENT_TIMEOUT_MS);
    return;
  }

  this->complete_measurement_cycle_();
}

void EnviroSenseCube::start_continuous_bsec_cycle_() {
  this->reset_cycle_measurements_();
  this->read_battery_();
  this->bsec_cycle_started_at_ = millis();
  if (!this->acquire_bsec_measurement_()) {
    ESP_LOGW(TAG, "Continuous BSEC2 cycle failed; scheduling a clean session reinitialization");
    this->abort_cycle_();
    return;
  }
  if (!this->bsec_measurement_ready_) {
    this->bsec_measurement_started_at_ = millis();
    this->state_deadline_ = this->bsec_measurement_started_at_ + BSEC_MEASUREMENT_TIMEOUT_MS;
    this->run_state_ = WAITING_FOR_BSEC;
    ESP_LOGI(TAG, "Continuous BSEC2 measurement started; waiting up to %u ms for gas data",
             BSEC_MEASUREMENT_TIMEOUT_MS);
    return;
  }

  this->complete_measurement_cycle_();
}

void EnviroSenseCube::abort_cycle_() {
  this->bsec_session_active_ = false;
  if (this->flag_(FLAG_OTA_MAINTENANCE_REQUESTED)) {
    this->save_support_data_();
    this->state_deadline_ = millis() + OTA_RECOVERY_RETRY_MS;
    this->run_state_ = WAITING_FOR_NEXT_CYCLE;
    ESP_LOGW(TAG, "OTA maintenance active; BSEC2 reinitialization scheduled in %u seconds",
             OTA_RECOVERY_RETRY_MS / 1000);
    return;
  }
  this->enter_sleep_();
}

void EnviroSenseCube::complete_measurement_cycle_() {
  if (!this->validate_measurements_()) {
    ESP_LOGW(TAG, "Cycle aborted; retrying after normal deep sleep");
    this->abort_cycle_();
    return;
  }

  this->update_last_static_iaq_();

  const bool environment_changed = this->environment_threshold_exceeded_();
  const bool battery_changed = this->battery_threshold_exceeded_();
  const bool periodic_display_refresh_due =
      this->support_data_.display_update_count >= DISPLAY_FULL_REFRESH_CYCLES;
  const bool full_refresh_due = periodic_display_refresh_due || this->manual_display_refresh_requested_;
  const bool heartbeat_due =
      this->support_data_.cycles_since_publish_attempt >= HEARTBEAT_CYCLES;
  const bool publish_due = environment_changed || battery_changed || heartbeat_due;

  ESP_LOGI(TAG,
           "Cycle decision: environment_changed=%s battery_changed=%s heartbeat_due=%s publish_due=%s "
           "cycles_since_attempt=%u",
           YESNO(environment_changed), YESNO(battery_changed), YESNO(heartbeat_due), YESNO(publish_due),
           this->support_data_.cycles_since_publish_attempt);

  if (environment_changed || battery_changed || full_refresh_due)
    this->set_flag_(FLAG_DISPLAY_UPDATE_PENDING, true);

  if (this->flag_(FLAG_DISPLAY_UPDATE_PENDING) &&
      (this->manual_display_refresh_requested_ ||
       this->support_data_.display_cycles_since_update >= DISPLAY_MIN_CYCLES)) {
    ESP_LOGI(TAG, "Display update starting (BUSY pin=%d)", digitalRead(BUSY_PIN));
    if (this->draw_display_(full_refresh_due)) {
      this->set_flag_(FLAG_DISPLAY_UPDATE_PENDING, false);
      this->manual_display_refresh_requested_ = false;
      this->support_data_.display_cycles_since_update = 0;
      ESP_LOGI(TAG, "Display update completed successfully");
    } else {
      this->support_data_.display_cycles_since_update = 0;
      ESP_LOGW(TAG, "Display update aborted; retry deferred by the normal display interval");
    }
  }

  if (heartbeat_due) {
    // GPIO2 drives both the active-low blue LED and the e-paper chip select.
    // Blink only after the display transaction has completed and SPI is idle.
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, LOW);
    delay(15);
    digitalWrite(CS_PIN, HIGH);
    ESP_LOGI(TAG, "Half-hour heartbeat: blue LED pulse completed");
  }

  if (publish_due) {
    ESP_LOGI(TAG, "Native API update required; enabling WiFi");
    this->store_publish_attempt_reference_();
    wifi::global_wifi_component->enable();
    this->state_deadline_ = millis() + WIFI_CONNECTION_TIMEOUT_MS;
    this->run_state_ = WAITING_FOR_WIFI;
  } else {
    this->finish_cycle_();
  }
}

bool EnviroSenseCube::validate_measurements_() {
  if (!this->environment_sensor_ready_)
    return false;

  if (!std::isfinite(this->temperature_) || !std::isfinite(this->humidity_) ||
      !std::isfinite(this->pressure_)) {
    ESP_LOGW(TAG, "BME688 returned invalid environmental values");
    return false;
  }

  if (this->temperature_ >= -20.0f && this->temperature_ <= 60.0f)
    this->battery_gauge_.set_cell_temperature(this->temperature_);

  ESP_LOGI(TAG,
           "Environment: temperature=%.2fC humidity=%.2f%% pressure=%.2fhPa mode=%s "
           "gas=%.0fohm static_iaq=%.0f adaptive_iaq=%.0f accuracy=%.0f",
           this->temperature_, this->humidity_, this->pressure_, "BSEC2",
           this->gas_resistance_, this->static_iaq_, this->adaptive_iaq_, this->iaq_accuracy_);
  return true;
}

bool EnviroSenseCube::acquire_bsec_measurement_() {
  this->bsec_measurement_ready_ = false;
  if (!this->bsec_.run()) {
    ESP_LOGW(TAG, "BSEC2 run failed: BSEC=%d BME68x=%d", this->bsec_.status, this->bsec_.sensor.status);
    return false;
  }
  if (this->bsec_measurement_ready_) {
    this->capture_bsec_state_();
    this->checkpoint_bsec_state_();
  }
  return true;
}

void EnviroSenseCube::bsec_data_callback_(const bme68xData data, const bsecOutputs outputs, SleepAwareBsec2 bsec) {
  (void) bsec;
  if (active_bsec_instance_ != nullptr)
    active_bsec_instance_->handle_bsec_data_(data, outputs);
}

void EnviroSenseCube::handle_bsec_data_(const bme68xData &data, const bsecOutputs &outputs) {
  if (!outputs.nOutputs)
    return;

  this->temperature_ = data.temperature + this->temperature_offset_;
  this->humidity_ = data.humidity + this->humidity_offset_;
  this->pressure_ = data.pressure + this->pressure_offset_;
  this->gas_resistance_ = data.gas_resistance;

  int8_t adaptive_accuracy = -1;
  int8_t static_accuracy = -1;
  for (uint8_t i = 0; i < outputs.nOutputs; i++) {
    const bsecData &output = outputs.output[i];
    switch (output.sensor_id) {
      case BSEC_OUTPUT_IAQ:
        this->adaptive_iaq_ = output.signal;
        this->iaq_accuracy_ = output.accuracy;
        adaptive_accuracy = output.accuracy;
        break;
      case BSEC_OUTPUT_STATIC_IAQ:
        this->static_iaq_ = output.signal;
        static_accuracy = output.accuracy;
        if (!std::isfinite(this->iaq_accuracy_) || output.accuracy > this->iaq_accuracy_)
          this->iaq_accuracy_ = output.accuracy;
        break;
      case BSEC_OUTPUT_CO2_EQUIVALENT:
        this->co2_equivalent_ = output.signal;
        break;
      case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
        this->breath_voc_equivalent_ = output.signal;
        break;
      case BSEC_OUTPUT_STABILIZATION_STATUS:
        this->bsec_stabilized_ = output.signal > 0.5f;
        break;
      case BSEC_OUTPUT_RUN_IN_STATUS:
        this->bsec_run_in_complete_ = output.signal > 0.5f;
        break;
      default:
        break;
    }
  }
  this->bsec_measurement_ready_ = std::isfinite(this->static_iaq_) &&
                                  std::isfinite(this->adaptive_iaq_) &&
                                  std::isfinite(this->gas_resistance_);
  ESP_LOGI(TAG,
           "BSEC2 outputs: time=%llums count=%u adaptive_accuracy=%d static_accuracy=%d stabilized=%s run_in=%s "
           "status=%d",
           static_cast<unsigned long long>(outputs.output[0].time_stamp / INT64_C(1000000)),
           outputs.nOutputs, adaptive_accuracy, static_accuracy, YESNO(this->bsec_stabilized_),
           YESNO(this->bsec_run_in_complete_), this->bsec_.status);
}

bool EnviroSenseCube::read_battery_() {
  float voltage_samples[BATTERY_SAMPLES];
  float percent_samples[BATTERY_SAMPLES];
  uint8_t valid_samples = 0;

  for (uint8_t i = 0; i < BATTERY_SAMPLES; i++) {
    float voltage;
    float percent;
    const bool valid = this->battery_gauge_.read_cell(voltage, percent) &&
                       std::isfinite(voltage) && std::isfinite(percent) &&
                       voltage >= 2.5f && voltage <= 4.5f &&
                       percent >= 0.0f && percent <= 100.0f;
    if (valid) {
      voltage_samples[valid_samples] = voltage;
      percent_samples[valid_samples] = percent;
      valid_samples++;
    }
    if (i + 1 < BATTERY_SAMPLES)
      delay(25);
  }

  this->battery_valid_ = valid_samples > 0;
  if (this->battery_valid_) {
    this->battery_voltage_ = this->median_(voltage_samples, valid_samples);
    this->battery_raw_ = this->median_(percent_samples, valid_samples);
    const uint8_t encoded = this->encode_half_unit_(this->battery_raw_);
    if (!this->flag_(FLAG_BATTERY_HISTORY_VALID)) {
      for (uint8_t &value : this->support_data_.battery_history)
        value = encoded;
      this->set_flag_(FLAG_BATTERY_HISTORY_VALID, true);
    } else {
      for (uint8_t i = 0; i + 1 < BATTERY_HISTORY_VALUES; i++)
        this->support_data_.battery_history[i] = this->support_data_.battery_history[i + 1];
      this->support_data_.battery_history[BATTERY_HISTORY_VALUES - 1] = encoded;
    }
  }

  if (this->flag_(FLAG_BATTERY_HISTORY_VALID)) {
    float history[BATTERY_HISTORY_VALUES];
    for (uint8_t i = 0; i < BATTERY_HISTORY_VALUES; i++)
      history[i] = this->decode_half_unit_(this->support_data_.battery_history[i]);
    this->battery_filtered_ = this->median_(history, BATTERY_HISTORY_VALUES);
  }

  if (this->battery_valid_) {
    ESP_LOGI(TAG, "Battery: valid_samples=%u voltage=%.3fV raw=%.1f%% filtered=%.1f%%",
             valid_samples, this->battery_voltage_, this->battery_raw_, this->battery_filtered_);
  } else {
    ESP_LOGW(TAG, "Battery: no valid sample");
  }
  return this->battery_valid_;
}

int16_t EnviroSenseCube::encode_temperature_reference_(float value) const {
  return static_cast<int16_t>(roundf(value * 10.0f));
}

uint8_t EnviroSenseCube::encode_half_unit_(float value) const {
  return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, roundf(value * 2.0f))));
}

float EnviroSenseCube::decode_half_unit_(uint8_t value) const { return value * 0.5f; }

float EnviroSenseCube::median_(const float values[], uint8_t count) const {
  float sorted[BATTERY_HISTORY_VALUES];
  for (uint8_t i = 0; i < count; i++)
    sorted[i] = values[i];
  for (uint8_t i = 1; i < count; i++) {
    const float value = sorted[i];
    int8_t position = i - 1;
    while (position >= 0 && sorted[position] > value) {
      sorted[position + 1] = sorted[position];
      position--;
    }
    sorted[position + 1] = value;
  }
  const uint8_t middle = count / 2;
  return count % 2 == 0 ? (sorted[middle - 1] + sorted[middle]) / 2.0f : sorted[middle];
}

void EnviroSenseCube::update_last_static_iaq_() {
  if (!std::isfinite(this->static_iaq_))
    return;
  this->support_data_.last_static_iaq_x2 =
      static_cast<uint8_t>(std::max(0.0f, std::min(250.0f, roundf(this->static_iaq_ * 0.5f))));
  this->set_flag_(FLAG_LAST_STATIC_IAQ_VALID, true);
}

bool EnviroSenseCube::environment_threshold_exceeded_() const {
  if (!this->flag_(FLAG_PUBLISH_REFERENCE_VALID))
    return true;
  const bool iaq_changed =
      std::isfinite(this->static_iaq_) &&
      fabsf(this->static_iaq_ - this->support_data_.publish_static_iaq) >= this->static_iaq_threshold_;
  return fabsf(this->temperature_ - this->support_data_.publish_temperature_x10 / 10.0f) >=
             this->temperature_threshold_ ||
         fabsf(this->humidity_ - this->support_data_.publish_humidity_x10 / 10.0f) >=
             this->humidity_threshold_ ||
         fabsf(this->pressure_ - this->support_data_.publish_pressure_x2 / 2.0f) >=
             this->pressure_threshold_ ||
         iaq_changed;
}

bool EnviroSenseCube::battery_threshold_exceeded_() const {
  if (!this->battery_valid_)
    return false;
  if (!this->flag_(FLAG_PUBLISH_BATTERY_REFERENCE_VALID))
    return true;
  return fabsf(this->battery_filtered_ - this->support_data_.publish_battery_x2 / 2.0f) >=
         this->battery_threshold_;
}

void EnviroSenseCube::store_publish_attempt_reference_() {
  this->support_data_.publish_temperature_x10 = this->encode_temperature_reference_(this->temperature_);
  this->support_data_.publish_humidity_x10 =
      static_cast<uint16_t>(std::max(0.0f, std::min(1000.0f, roundf(this->humidity_ * 10.0f))));
  this->support_data_.publish_pressure_x2 =
      static_cast<uint16_t>(std::max(0.0f, std::min(65535.0f, roundf(this->pressure_ * 2.0f))));
  if (std::isfinite(this->static_iaq_))
    this->support_data_.publish_static_iaq =
        static_cast<uint16_t>(std::max(0.0f, std::min(500.0f, roundf(this->static_iaq_))));
  this->set_flag_(FLAG_PUBLISH_REFERENCE_VALID, true);
  if (this->battery_valid_) {
    this->support_data_.publish_battery_x2 = this->encode_half_unit_(this->battery_filtered_);
    this->set_flag_(FLAG_PUBLISH_BATTERY_REFERENCE_VALID, true);
  }
  this->support_data_.cycles_since_publish_attempt = 0;
}

void EnviroSenseCube::publish_states_() {
  ESP_LOGI(TAG,
           "Publishing BSEC cycle: T=%.2fC H=%.2f%% P=%.2fhPa gas=%.0fohm static_iaq=%.0f "
           "adaptive_iaq=%.0f accuracy=%.0f battery_valid=%s battery=%.1f%%",
           this->temperature_, this->humidity_, this->pressure_, this->gas_resistance_, this->static_iaq_,
           this->adaptive_iaq_, this->iaq_accuracy_, YESNO(this->battery_valid_), this->battery_filtered_);
  if (std::isfinite(this->temperature_))
    this->temperature_sensor_->publish_state(this->temperature_);
  if (std::isfinite(this->humidity_))
    this->humidity_sensor_->publish_state(this->humidity_);
  if (std::isfinite(this->pressure_))
    this->pressure_sensor_->publish_state(this->pressure_);
  if (std::isfinite(this->gas_resistance_))
    this->gas_resistance_sensor_->publish_state(this->gas_resistance_);
  if (std::isfinite(this->static_iaq_))
    this->static_iaq_sensor_->publish_state(this->static_iaq_);
  if (std::isfinite(this->adaptive_iaq_))
    this->adaptive_iaq_sensor_->publish_state(this->adaptive_iaq_);
  if (std::isfinite(this->iaq_accuracy_))
    this->iaq_accuracy_sensor_->publish_state(this->iaq_accuracy_);
  if (std::isfinite(this->co2_equivalent_))
    this->co2_equivalent_sensor_->publish_state(this->co2_equivalent_);
  if (std::isfinite(this->breath_voc_equivalent_))
    this->breath_voc_equivalent_sensor_->publish_state(this->breath_voc_equivalent_);
  if (this->bsec_measurement_ready_) {
    this->bsec_stabilized_sensor_->publish_state(this->bsec_stabilized_);
    this->bsec_run_in_sensor_->publish_state(this->bsec_run_in_complete_);
  }
  this->battery_valid_sensor_->publish_state(this->battery_valid_);
  this->ota_maintenance_sensor_->publish_state(this->flag_(FLAG_OTA_MAINTENANCE_REQUESTED));
  if (this->battery_valid_) {
    this->battery_sensor_->publish_state(this->battery_filtered_);
    this->battery_raw_sensor_->publish_state(this->battery_raw_);
    this->battery_voltage_sensor_->publish_state(this->battery_voltage_);
  }
}

void EnviroSenseCube::finish_cycle_() {
  if (this->flag_(FLAG_OTA_MAINTENANCE_REQUESTED)) {
    if (this->support_data_.display_cycles_since_update < DISPLAY_MIN_CYCLES)
      this->support_data_.display_cycles_since_update++;
    if (this->support_data_.cycles_since_publish_attempt < HEARTBEAT_CYCLES)
      this->support_data_.cycles_since_publish_attempt++;
    if (this->support_data_.display_update_count < DISPLAY_FULL_REFRESH_CYCLES)
      this->support_data_.display_update_count++;
    this->save_support_data_();
    this->cold_boot_ = false;
    this->state_deadline_ = this->bsec_cycle_started_at_ + BSEC_CYCLE_INTERVAL_MS;
    this->run_state_ = WAITING_FOR_NEXT_CYCLE;
    const uint32_t now = millis();
    const uint32_t remaining_ms = static_cast<int32_t>(this->state_deadline_ - now) > 0
                                      ? this->state_deadline_ - now
                                      : 0;
    ESP_LOGI(TAG,
             "OTA maintenance active; live BSEC2 state saved and next cycle aligned in %u seconds",
             remaining_ms / 1000);
    return;
  }
  this->enter_sleep_();
}

void EnviroSenseCube::enter_sleep_() {
  if (this->support_data_.display_cycles_since_update < DISPLAY_MIN_CYCLES)
    this->support_data_.display_cycles_since_update++;
  if (this->support_data_.cycles_since_publish_attempt < HEARTBEAT_CYCLES)
    this->support_data_.cycles_since_publish_attempt++;
  if (this->support_data_.display_update_count < DISPLAY_FULL_REFRESH_CYCLES)
    this->support_data_.display_update_count++;
  this->save_support_data_();
  const uint32_t active_ms = millis() - this->bsec_cycle_started_at_;
  uint32_t sleep_ms = active_ms < BSEC_CYCLE_INTERVAL_MS
                          ? BSEC_CYCLE_INTERVAL_MS - active_ms
                          : MINIMUM_DEEP_SLEEP_MS;
  if (this->bsec_session_active_) {
    const int64_t until_next_call = this->bsec_.nextCallMs() - this->bsec_.getTimeMs();
    sleep_ms = until_next_call > 0
                   ? static_cast<uint32_t>(std::min<int64_t>(until_next_call, BSEC_CYCLE_INTERVAL_MS))
                   : MINIMUM_DEEP_SLEEP_MS;
  }
  if (!this->save_bsec_clock_(sleep_ms)) {
    // Do not knowingly lose the clock. Keep the session alive until the next cycle.
    this->state_deadline_ = millis() + sleep_ms;
    this->run_state_ = WAITING_FOR_NEXT_CYCLE;
    return;
  }
  ESP_LOGI(TAG, "State saved; requesting %u-second deep sleep to preserve the 300-second BSEC2 cadence",
           sleep_ms / 1000);
  this->run_state_ = FINISHED;
  if (this->deep_sleep_ != nullptr) {
    this->deep_sleep_->set_sleep_duration(sleep_ms);
    this->deep_sleep_->begin_sleep();
  }
}

void EnviroSenseCube::draw_decimal_value_(GFXcanvas1 &black_canvas, GFXcanvas1 &red_canvas,
                                          float value, int16_t text_y, const uint8_t *icon,
                                          int16_t fixed_icon_x) {
  static constexpr uint8_t INTEGER_TEXT_HEIGHT = 3;
  static constexpr uint8_t FRACTION_TEXT_WIDTH = 2;
  static constexpr uint8_t FRACTION_TEXT_HEIGHT = 2;
  static constexpr int16_t ICON_WIDTH = 12;
  static constexpr int16_t ICON_HEIGHT = 17;
  static constexpr int16_t ICON_GAP = 2;
  const int32_t scaled = static_cast<int32_t>(lroundf(value * 10.0f));
  const uint32_t magnitude = scaled < 0 ? static_cast<uint32_t>(-scaled) : static_cast<uint32_t>(scaled);
  char integer_text[8];
  snprintf(integer_text, sizeof(integer_text), scaled < 0 ? "-%lu" : "%lu",
           static_cast<unsigned long>(magnitude / 10));
  const uint8_t integer_text_width = strlen(integer_text) > 2 ? 2 : 3;
  const int16_t integer_width = strlen(integer_text) * 6 * integer_text_width;
  const int16_t fraction_width = 6 * FRACTION_TEXT_WIDTH;
  const int16_t number_width = integer_width + 3 + fraction_width;
  const int16_t number_x = fixed_icon_x - ICON_GAP - number_width;
  const int16_t fraction_y = text_y + 8;
  red_canvas.drawBitmap(fixed_icon_x, text_y + 3, icon, ICON_WIDTH, ICON_HEIGHT, 1);
  black_canvas.setTextSize(integer_text_width, INTEGER_TEXT_HEIGHT);
  black_canvas.setCursor(number_x, text_y);
  black_canvas.print(integer_text);
  black_canvas.fillRect(number_x + integer_width, text_y + 19, 2, 2, 0);
  black_canvas.setTextSize(FRACTION_TEXT_WIDTH, FRACTION_TEXT_HEIGHT);
  black_canvas.setCursor(number_x + integer_width + 3, fraction_y);
  black_canvas.print(static_cast<uint8_t>(magnitude % 10));
}

void EnviroSenseCube::draw_integer_value_(GFXcanvas1 &red_canvas, GFXcanvas1 &target,
                                          float value, int16_t text_y, const uint8_t *icon,
                                          uint8_t text_width, int16_t fixed_icon_x) {
  static constexpr uint8_t INTEGER_TEXT_HEIGHT = 3;
  static constexpr int16_t ICON_WIDTH = 12;
  static constexpr int16_t ICON_HEIGHT = 17;
  static constexpr int16_t ICON_GAP = 2;
  char value_text[8];
  if (std::isfinite(value))
    snprintf(value_text, sizeof(value_text), "%u", static_cast<uint16_t>(roundf(value)));
  else
    snprintf(value_text, sizeof(value_text), "---");
  const int16_t number_width = strlen(value_text) * 6 * text_width;
  const int16_t number_x = fixed_icon_x - ICON_GAP - number_width;
  red_canvas.drawBitmap(fixed_icon_x, text_y + 3, icon, ICON_WIDTH, ICON_HEIGHT, 1);
  target.setTextSize(text_width, INTEGER_TEXT_HEIGHT);
  target.setCursor(number_x, text_y);
  target.print(value_text);
}

void EnviroSenseCube::draw_display_content_(GFXcanvas1 &black_canvas, GFXcanvas1 &red_canvas,
                                            float temperature, float humidity, float pressure,
                                            float static_iaq, float battery, bool battery_valid) {
  black_canvas.setFont();
  black_canvas.setTextColor(0);
  black_canvas.fillScreen(1);
  red_canvas.setFont();
  red_canvas.setTextColor(1);
  red_canvas.fillScreen(0);

  // Two rows of measurements. Values remain black; icons and short accents
  // use the red plane. The right edge of each icon is kept inside its cell.
  this->draw_decimal_value_(black_canvas, red_canvas, temperature, 12, ICON_TEMPERATURE, 61);
  this->draw_decimal_value_(black_canvas, red_canvas, humidity, 12, ICON_HUMIDITY, 137);
  this->draw_integer_value_(red_canvas, black_canvas, pressure, 80, ICON_PRESSURE, 2, 61);
  GFXcanvas1 &iaq_canvas =
      std::isfinite(static_iaq) && static_iaq >= 100.0f ? red_canvas : black_canvas;
  this->draw_integer_value_(red_canvas, iaq_canvas, static_iaq, 80, ICON_AIR_QUALITY, 3, 137);

  red_canvas.drawFastHLine(21, 46, 34, 1);
  red_canvas.drawFastHLine(97, 46, 34, 1);
  red_canvas.drawFastHLine(21, 114, 34, 1);
  red_canvas.drawFastHLine(97, 114, 34, 1);

  // Compact battery indicator along the lower edge.
  static constexpr int16_t BATTERY_ICON_X = 2;
  static constexpr int16_t BATTERY_ICON_Y = 140;
  static constexpr int16_t BATTERY_ICON_BODY_WIDTH = 12;
  static constexpr int16_t BATTERY_ICON_HEIGHT = 8;
  static constexpr int16_t BATTERY_BAR_X = 20;
  static constexpr int16_t BATTERY_BAR_Y = 141;
  static constexpr int16_t BATTERY_BAR_HEIGHT = 7;
  static constexpr int16_t BATTERY_BAR_WIDTH = DISPLAY_WIDTH - BATTERY_BAR_X - 2;

  red_canvas.drawRect(BATTERY_ICON_X, BATTERY_ICON_Y, BATTERY_ICON_BODY_WIDTH,
                      BATTERY_ICON_HEIGHT, 1);
  red_canvas.drawRect(BATTERY_ICON_X + 1, BATTERY_ICON_Y + 1,
                      BATTERY_ICON_BODY_WIDTH - 2, BATTERY_ICON_HEIGHT - 2, 1);
  red_canvas.fillRect(BATTERY_ICON_X + BATTERY_ICON_BODY_WIDTH,
                      BATTERY_ICON_Y + 2, 2, 4, 1);
  black_canvas.drawRect(BATTERY_BAR_X, BATTERY_BAR_Y, BATTERY_BAR_WIDTH,
                        BATTERY_BAR_HEIGHT, 0);

  if (battery_valid && std::isfinite(battery)) {
    const float bounded_battery = std::max(0.0f, std::min(100.0f, battery));
    const int16_t battery_width = static_cast<int16_t>(
        roundf(bounded_battery * static_cast<float>(BATTERY_BAR_WIDTH - 4) / 100.0f));
    if (battery_width > 0) {
      if (bounded_battery <= 20.0f)
        red_canvas.fillRect(BATTERY_BAR_X + 2, BATTERY_BAR_Y + 2, battery_width, 3, 1);
      else
        black_canvas.fillRect(BATTERY_BAR_X + 2, BATTERY_BAR_Y + 2, battery_width, 3, 0);
    }
  }
}

void EnviroSenseCube::capture_display_snapshot_() {
  DisplaySnapshot &snapshot = this->support_data_.display_snapshot;
  snapshot.temperature_x10 = static_cast<int16_t>(roundf(this->temperature_ * 10.0f));
  snapshot.humidity_x10 =
      static_cast<uint16_t>(std::max(0.0f, std::min(6553.5f, this->humidity_)) * 10.0f + 0.5f);
  snapshot.pressure_x10 =
      static_cast<uint16_t>(std::max(0.0f, std::min(6553.5f, this->pressure_)) * 10.0f + 0.5f);
  snapshot.static_iaq = std::isfinite(this->static_iaq_)
                            ? static_cast<uint16_t>(std::max(
                                  0.0f, std::min(500.0f, roundf(this->static_iaq_))))
                            : 0;
  snapshot.battery_x2 = this->battery_valid_ && std::isfinite(this->battery_filtered_)
                            ? this->encode_half_unit_(this->battery_filtered_)
                            : 0;
  snapshot.flags = DISPLAY_SNAPSHOT_VALID | DISPLAY_SNAPSHOT_LAYOUT_CURRENT;
  if (this->battery_valid_ && std::isfinite(this->battery_filtered_))
    snapshot.flags |= DISPLAY_SNAPSHOT_BATTERY_VALID;
  if (std::isfinite(this->static_iaq_))
    snapshot.flags |= DISPLAY_SNAPSHOT_IAQ_VALID;
}

bool EnviroSenseCube::draw_display_(bool force_full_refresh) {
  (void) force_full_refresh;
  GFXcanvas1 black_canvas(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  GFXcanvas1 red_canvas(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  if (black_canvas.getBuffer() == nullptr || red_canvas.getBuffer() == nullptr) {
    ESP_LOGW(TAG, "Tri-colour display buffer allocation failed; free heap=%u", ESP.getFreeHeap());
    return false;
  }
  this->draw_display_content_(black_canvas, red_canvas, this->temperature_, this->humidity_,
                              this->pressure_, this->static_iaq_, this->battery_filtered_,
                              this->battery_valid_);

  pinMode(RST_PIN, OUTPUT);
  pinMode(DC_PIN, OUTPUT);
  pinMode(CS_PIN, OUTPUT);
  SPI.begin();
  pinMode(BUSY_PIN, INPUT);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

  const uint32_t display_started_at = millis();
  ESP_LOGI(TAG, "E-paper transaction: E2154JS0C1 black/white/red global refresh BUSY=%d",
           digitalRead(BUSY_PIN));
  e2154js0c1::begin_operation(DISPLAY_OPERATION_TIMEOUT_MS);
  const int8_t panel_temperature = std::isfinite(this->temperature_)
                                       ? static_cast<int8_t>(lroundf(this->temperature_))
                                       : 25;
  bool display_ok =
      e2154js0c1::update(black_canvas.getBuffer(), red_canvas.getBuffer(), panel_temperature);
  if (display_ok && (e2154js0c1::operation_timed_out() ||
                     millis() - display_started_at >= DISPLAY_OPERATION_TIMEOUT_MS)) {
    ESP_LOGE(TAG, "E-paper total operation limit reached after %lu ms", millis() - display_started_at);
    display_ok = false;
  }
  if (!display_ok)
    e2154js0c1::force_power_off();
  e2154js0c1::end_operation();

  SPI.endTransaction();
  SPI.end();
  if (display_ok) {
    this->capture_display_snapshot_();
    this->support_data_.display_update_count = 0;
  }
  ESP_LOGI(TAG, "E-paper transaction finished: mode=global-bwr success=%s elapsed=%lu ms BUSY=%d",
           YESNO(display_ok), millis() - display_started_at, digitalRead(BUSY_PIN));
  return display_ok;
}

}  // namespace envirosense_cube
}  // namespace esphome
