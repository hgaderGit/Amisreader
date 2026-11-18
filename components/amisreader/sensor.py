import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, text_sensor, uart
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_REACTIVE_ENERGY,
    DEVICE_CLASS_REACTIVE_POWER,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_KILOVOLT_AMPS_REACTIVE_HOURS,
    UNIT_KILOWATT_HOURS,
    UNIT_VOLT_AMPS_REACTIVE,
    UNIT_WATT,
)

CODEOWNERS = ["@hgaderGit"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["text_sensor"]

# Namespace und Klasse
amis_ns = cg.esphome_ns.namespace("amis")
Amis = amis_ns.class_("Amis", cg.Component, uart.UARTDevice)

CONF_AES_KEY = "aes_key"
CONF_MQTT_JSON_TOPIC = "mqtt_json_topic"
CONF_DATE_TIME = "date_time"


def aes_key(value):
    """Der AES-Key muss aus genau 32 Hex-Zeichen (16 Byte) bestehen."""
    value = cv.string_strict(value).strip()
    if len(value) != 32 or any(c not in "0123456789abcdefABCDEF" for c in value):
        raise cv.Invalid("aes_key muss aus genau 32 Hex-Zeichen (16 Byte) bestehen")
    return value


def _power():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_WATT,
        device_class=DEVICE_CLASS_POWER,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    )


def _reactive_power():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT_AMPS_REACTIVE,
        device_class=DEVICE_CLASS_REACTIVE_POWER,
        state_class=STATE_CLASS_MEASUREMENT,
        accuracy_decimals=0,
    )


def _energy():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOWATT_HOURS,
        device_class=DEVICE_CLASS_ENERGY,
        state_class=STATE_CLASS_TOTAL_INCREASING,
        accuracy_decimals=3,
    )


def _reactive_energy():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOVOLT_AMPS_REACTIVE_HOURS,
        device_class=DEVICE_CLASS_REACTIVE_ENERGY,
        state_class=STATE_CLASS_TOTAL_INCREASING,
        accuracy_decimals=3,
    )


# YAML-Schluessel -> Sensor-Schema. Der Setter in C++ heisst set_<schluessel>_sensor().
SENSORS = {
    "power": _power(),
    "power_forward": _power(),
    "power_reverse": _power(),
    "reactive_power_import": _reactive_power(),
    "reactive_power_export": _reactive_power(),
    "energy_forward": _energy(),
    "energy_reverse": _energy(),
    "reactive_energy_import": _reactive_energy(),
    "reactive_energy_export": _reactive_energy(),
    # Inkasso-Energie ist nicht verifiziert: bewusst ohne state_class
    "energy_inkasso": sensor.sensor_schema(
        unit_of_measurement=UNIT_KILOWATT_HOURS,
        device_class=DEVICE_CLASS_ENERGY,
        accuracy_decimals=3,
    ),
    # Berechnet aus Zaehlerstand (1-0:1.8.0) und Zaehlerzeit, siehe README
    "power_quarter_hour": _power(),  # laufende Viertelstunde (1-0:1.4.0)
    "power_last_quarter_hour": _power(),  # letzte Viertelstunde (1-0:1.5.0)
    "power_max_month": _power(),  # Monatsmaximum (1-0:1.6.0)
}

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Amis),
            cv.Required(CONF_AES_KEY): aes_key,
            cv.Optional(CONF_MQTT_JSON_TOPIC): cv.publish_topic,
            cv.Optional(CONF_DATE_TIME): text_sensor.text_sensor_schema(),
            **{cv.Optional(key): schema for key, schema in SENSORS.items()},
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA),
    # Verwendet mbedtls und esp_timer: nur ESP32
    cv.only_on(["esp32"]),
)

# Der Zaehler sendet mit 9600 Baud, 8E1. Falsche UART-Einstellungen werden schon beim Kompilieren gemeldet.
FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "amisreader",
    baud_rate=9600,
    require_rx=True,
    data_bits=8,
    parity="EVEN",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Key (32 Hex-Zeichen) in 16 Bytes umwandeln
    cg.add(var.set_aes_key(list(bytes.fromhex(config[CONF_AES_KEY]))))

    if CONF_MQTT_JSON_TOPIC in config:
        cg.add(var.set_mqtt_topic(config[CONF_MQTT_JSON_TOPIC]))

    # Sensoren registrieren; erzeugt dynamisch den Aufruf var.set_<key>_sensor(sens)
    for key in SENSORS:
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, f"set_{key}_sensor")(sens))

    # Zaehlerzeit als Text-Sensor
    if CONF_DATE_TIME in config:
        sens = await text_sensor.new_text_sensor(config[CONF_DATE_TIME])
        cg.add(var.set_date_time_sensor(sens))
