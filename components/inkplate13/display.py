import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.components import spi, display
from esphome import pins
from esphome.const import CONF_ID, CONF_LAMBDA

DEPENDENCIES = ["spi", "display"]

inkplate13_ns = cg.esphome_ns.namespace("inkplate13")
Inkplate13 = inkplate13_ns.class_(
    "Inkplate13", display.Display, spi.SPIDevice
)

CONF_RST_PIN    = "rst_pin"
CONF_DC_PIN     = "dc_pin"
CONF_BUSY_PIN   = "busy_pin"
CONF_PWR_EN_PIN = "pwr_en_pin"
CONF_CS_M_PIN   = "cs_m_pin"
CONF_CS_S_PIN   = "cs_s_pin"
CONF_BS0_PIN    = "bs0_pin"
CONF_BS1_PIN    = "bs1_pin"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Inkplate13),
            cv.Required(CONF_CS_M_PIN):   pins.gpio_output_pin_schema,
            cv.Required(CONF_CS_S_PIN):   pins.gpio_output_pin_schema,
            cv.Required(CONF_RST_PIN):    pins.gpio_output_pin_schema,
            cv.Required(CONF_DC_PIN):     pins.gpio_output_pin_schema,
            cv.Required(CONF_BUSY_PIN):   pins.gpio_input_pin_schema,
            cv.Required(CONF_PWR_EN_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_BS0_PIN):    pins.gpio_output_pin_schema,
            cv.Required(CONF_BS1_PIN):    pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(display.FULL_DISPLAY_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=False))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await display.register_display(var, config)
    await spi.register_spi_device(var, config)

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA],
            [(display.display_ns.class_("Display").operator("ref"), "it")],
            return_type=cg.void,
        )
        cg.add(var.set_writer(lambda_))

    for conf_key, setter in [
        (CONF_RST_PIN,    "set_rst_pin"),
        (CONF_DC_PIN,     "set_dc_pin"),
        (CONF_BUSY_PIN,   "set_busy_pin"),
        (CONF_PWR_EN_PIN, "set_pwr_en_pin"),
        (CONF_CS_M_PIN,   "set_cs_m_pin"),
        (CONF_CS_S_PIN,   "set_cs_s_pin"),
        (CONF_BS0_PIN,    "set_bs0_pin"),
        (CONF_BS1_PIN,    "set_bs1_pin"),
    ]:
        pin = await cg.gpio_pin_expression(config[conf_key])
        cg.add(getattr(var, setter)(pin))
