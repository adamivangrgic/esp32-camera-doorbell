import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, text_sensor
from esphome.const import CONF_ID, CONF_UPDATE_INTERVAL

DEPENDENCIES = []          # No wifi/eth dependency — works with either
AUTO_LOAD = ["binary_sensor", "text_sensor"]

esp32_two_way_audio_ns = cg.esphome_ns.namespace("esp32_two_way_audio")
ESP32TwoWayAudio = esp32_two_way_audio_ns.class_(
    "ESP32TwoWayAudio", cg.PollingComponent
)

CONF_BCK_PIN        = "bck_pin"
CONF_WS_PIN         = "ws_pin"
CONF_DATA_OUT_PIN   = "data_out_pin"
CONF_DATA_IN_PIN    = "data_in_pin"
CONF_COM_PORT       = "com_port"
CONF_MIC_SWITCH     = "microphone_switch"
CONF_SPK_SWITCH     = "speaker_switch"
CONF_TONE_SWITCH    = "call_tone_switch"
CONF_PARTNER_IP     = "partner_ip_sensor"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ESP32TwoWayAudio),
        # I2S pins — all required so nothing is hidden/hardcoded
        cv.Required(CONF_BCK_PIN):      cv.positive_int,
        cv.Required(CONF_WS_PIN):       cv.positive_int,
        cv.Required(CONF_DATA_OUT_PIN): cv.positive_int,
        cv.Required(CONF_DATA_IN_PIN):  cv.positive_int,
        # Network
        cv.Optional(CONF_COM_PORT, default=8000): cv.port,
        # Linked sensors / switches
        cv.Optional(CONF_MIC_SWITCH):  cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_SPK_SWITCH):  cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_TONE_SWITCH): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_PARTNER_IP):  cv.use_id(text_sensor.TextSensor),
        # Polling interval (default 10 ms — same as before)
        cv.Optional(CONF_UPDATE_INTERVAL, default="10ms"): cv.update_interval,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_bck_pin(config[CONF_BCK_PIN]))
    cg.add(var.set_ws_pin(config[CONF_WS_PIN]))
    cg.add(var.set_data_out_pin(config[CONF_DATA_OUT_PIN]))
    cg.add(var.set_data_in_pin(config[CONF_DATA_IN_PIN]))
    cg.add(var.set_com_port(config[CONF_COM_PORT]))

    if CONF_MIC_SWITCH in config:
        sens = await cg.get_variable(config[CONF_MIC_SWITCH])
        cg.add(var.set_microphone_switch(sens))

    if CONF_SPK_SWITCH in config:
        sens = await cg.get_variable(config[CONF_SPK_SWITCH])
        cg.add(var.set_speaker_switch(sens))

    if CONF_TONE_SWITCH in config:
        sens = await cg.get_variable(config[CONF_TONE_SWITCH])
        cg.add(var.set_call_tone_switch(sens))

    if CONF_PARTNER_IP in config:
        sens = await cg.get_variable(config[CONF_PARTNER_IP])
        cg.add(var.set_partner_ip_sensor(sens))
