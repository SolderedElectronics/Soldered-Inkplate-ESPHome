import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.components import spi, display
from esphome.const import CONF_ID, CONF_LAMBDA

DEPENDENCIES = ["spi", "display"]

inkplate13_ns = cg.esphome_ns.namespace("inkplate13")
Inkplate13 = inkplate13_ns.class_(
    "Inkplate13", display.Display, spi.SPIDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Inkplate13),
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
