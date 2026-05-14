import esphome.config_validation as cv
import esphome.codegen as cg

from esphome.const import CONF_ID

inkplate13_ns = cg.esphome_ns.namespace("inkplate13")
Inkplate13 = inkplate13_ns.class_("Inkplate13", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Inkplate13),
})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
