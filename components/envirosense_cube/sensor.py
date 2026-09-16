import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, deep_sleep, sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_ATMOSPHERIC_PRESSURE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CARBON_DIOXIDE,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLATILE_ORGANIC_COMPOUNDS_PARTS,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_HECTOPASCAL,
    UNIT_OHM,
    UNIT_PARTS_PER_MILLION,
    UNIT_PERCENT,
    UNIT_VOLT,
)

DEPENDENCIES = ["wifi", "api", "deep_sleep"]
AUTO_LOAD = ["binary_sensor"]

CONF_DEEP_SLEEP_ID = "deep_sleep_id"
CONF_TEMPERATURE_OFFSET = "temperature_offset"
CONF_HUMIDITY_OFFSET = "humidity_offset"
CONF_PRESSURE_OFFSET = "pressure_offset"
CONF_TEMPERATURE_THRESHOLD = "temperature_threshold"
CONF_HUMIDITY_THRESHOLD = "humidity_threshold"
CONF_PRESSURE_THRESHOLD = "pressure_threshold"
CONF_STATIC_IAQ_THRESHOLD = "static_iaq_threshold"
CONF_BATTERY_THRESHOLD = "battery_threshold"
CONF_TEMPERATURE = "temperature"
CONF_HUMIDITY = "humidity"
CONF_PRESSURE = "pressure"
CONF_GAS_RESISTANCE = "gas_resistance"
CONF_STATIC_IAQ = "static_iaq"
CONF_ADAPTIVE_IAQ = "adaptive_iaq"
CONF_IAQ_ACCURACY = "iaq_accuracy"
CONF_CO2_EQUIVALENT = "co2_equivalent"
CONF_BREATH_VOC_EQUIVALENT = "breath_voc_equivalent"
CONF_BSEC_STABILIZED = "bsec_stabilized"
CONF_BSEC_RUN_IN_COMPLETE = "bsec_run_in_complete"
CONF_BATTERY = "battery"
CONF_BATTERY_RAW = "battery_raw"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_BATTERY_VALID = "battery_valid"
CONF_OTA_MAINTENANCE_ACTIVE = "ota_maintenance_active"

envirosense_ns = cg.esphome_ns.namespace("envirosense_cube")
EnviroSenseCube = envirosense_ns.class_("EnviroSenseCube", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EnviroSenseCube),
        cv.Required(CONF_DEEP_SLEEP_ID): cv.use_id(deep_sleep.DeepSleepComponent),
        cv.Required(CONF_TEMPERATURE_OFFSET): cv.float_range(min=-20.0, max=20.0),
        cv.Required(CONF_HUMIDITY_OFFSET): cv.float_range(min=-50.0, max=50.0),
        cv.Required(CONF_PRESSURE_OFFSET): cv.float_range(min=-100.0, max=100.0),
        cv.Required(CONF_TEMPERATURE_THRESHOLD): cv.float_range(min=0.0),
        cv.Required(CONF_HUMIDITY_THRESHOLD): cv.float_range(min=0.0),
        cv.Required(CONF_PRESSURE_THRESHOLD): cv.float_range(min=0.0),
        cv.Required(CONF_STATIC_IAQ_THRESHOLD): cv.float_range(min=0.0),
        cv.Required(CONF_BATTERY_THRESHOLD): cv.float_range(min=0.0, max=100.0),
        cv.Required(CONF_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_HUMIDITY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_HUMIDITY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_PRESSURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_HECTOPASCAL,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_ATMOSPHERIC_PRESSURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_GAS_RESISTANCE): sensor.sensor_schema(
            unit_of_measurement=UNIT_OHM,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_STATIC_IAQ): sensor.sensor_schema(
            unit_of_measurement="IAQ",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:gauge",
        ),
        cv.Required(CONF_ADAPTIVE_IAQ): sensor.sensor_schema(
            unit_of_measurement="IAQ",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            icon="mdi:gauge",
        ),
        cv.Required(CONF_IAQ_ACCURACY): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:checkbox-marked-circle-outline",
        ),
        cv.Required(CONF_CO2_EQUIVALENT): sensor.sensor_schema(
            unit_of_measurement=UNIT_PARTS_PER_MILLION,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_CARBON_DIOXIDE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_BREATH_VOC_EQUIVALENT): sensor.sensor_schema(
            unit_of_measurement=UNIT_PARTS_PER_MILLION,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_VOLATILE_ORGANIC_COMPOUNDS_PARTS,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_BATTERY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_BATTERY_RAW): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Required(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            accuracy_decimals=3,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Required(CONF_BATTERY_VALID): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:battery-check",
        ),
        cv.Required(CONF_BSEC_STABILIZED): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:progress-check",
        ),
        cv.Required(CONF_BSEC_RUN_IN_COMPLETE): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:timer-check-outline",
        ),
        cv.Required(CONF_OTA_MAINTENANCE_ACTIVE): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:update",
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_platformio_option("lib_ldf_mode", "deep+")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    sleep = await cg.get_variable(config[CONF_DEEP_SLEEP_ID])
    cg.add(var.set_deep_sleep(sleep))

    configuration_setters = (
        (CONF_TEMPERATURE_OFFSET, "set_temperature_offset"),
        (CONF_HUMIDITY_OFFSET, "set_humidity_offset"),
        (CONF_PRESSURE_OFFSET, "set_pressure_offset"),
        (CONF_TEMPERATURE_THRESHOLD, "set_temperature_threshold"),
        (CONF_HUMIDITY_THRESHOLD, "set_humidity_threshold"),
        (CONF_PRESSURE_THRESHOLD, "set_pressure_threshold"),
        (CONF_STATIC_IAQ_THRESHOLD, "set_static_iaq_threshold"),
        (CONF_BATTERY_THRESHOLD, "set_battery_threshold"),
    )
    for key, setter in configuration_setters:
        cg.add(getattr(var, setter)(config[key]))

    setters = (
        (CONF_TEMPERATURE, "set_temperature_sensor"),
        (CONF_HUMIDITY, "set_humidity_sensor"),
        (CONF_PRESSURE, "set_pressure_sensor"),
        (CONF_GAS_RESISTANCE, "set_gas_resistance_sensor"),
        (CONF_STATIC_IAQ, "set_static_iaq_sensor"),
        (CONF_ADAPTIVE_IAQ, "set_adaptive_iaq_sensor"),
        (CONF_IAQ_ACCURACY, "set_iaq_accuracy_sensor"),
        (CONF_CO2_EQUIVALENT, "set_co2_equivalent_sensor"),
        (CONF_BREATH_VOC_EQUIVALENT, "set_breath_voc_equivalent_sensor"),
        (CONF_BATTERY, "set_battery_sensor"),
        (CONF_BATTERY_RAW, "set_battery_raw_sensor"),
        (CONF_BATTERY_VOLTAGE, "set_battery_voltage_sensor"),
    )
    for key, setter in setters:
        sens = await sensor.new_sensor(config[key])
        cg.add(getattr(var, setter)(sens))

    valid = await binary_sensor.new_binary_sensor(config[CONF_BATTERY_VALID])
    cg.add(var.set_battery_valid_sensor(valid))

    stabilized = await binary_sensor.new_binary_sensor(config[CONF_BSEC_STABILIZED])
    cg.add(var.set_bsec_stabilized_sensor(stabilized))

    run_in = await binary_sensor.new_binary_sensor(config[CONF_BSEC_RUN_IN_COMPLETE])
    cg.add(var.set_bsec_run_in_sensor(run_in))

    ota_active = await binary_sensor.new_binary_sensor(config[CONF_OTA_MAINTENANCE_ACTIVE])
    cg.add(var.set_ota_maintenance_sensor(ota_active))

    cg.add_library(
        "BME68x Sensor library",
        "1.3.40408",
        "https://github.com/boschsensortec/Bosch-BME68x-Library",
    )
    cg.add_library(
        "BSEC2 Software Library",
        None,
        "https://github.com/boschsensortec/Bosch-BSEC2-Library.git#1.10.2610",
    )
    cg.add_define("USE_BSEC2")
    cg.add_library("adafruit/Adafruit LC709203F", "1.3.4")
    cg.add_library("adafruit/Adafruit GFX Library", "1.11.11")
    cg.add_library("adafruit/Adafruit BusIO", "1.16.1")
    cg.add_library("adafruit/Adafruit Unified Sensor", "1.1.14")
    cg.add_library("SPI", None)
    cg.add_library("Wire", None)
