import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.components import spi
from esphome.const import CONF_ID

DEPENDENCIES = ["spi"]

inkplate13_ns = cg.esphome_ns.namespace("inkplate13")
Inkplate13 = inkplate13_ns.class_(
    "Inkplate13", cg.Component, spi.SPIDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
        cv.GenerateID(): cv.declare_id(Inkplate13),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)

 

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
