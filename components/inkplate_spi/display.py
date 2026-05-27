import pkgutil
import importlib

import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.components import spi, display
from esphome.const import CONF_ID, CONF_LAMBDA, CONF_MODEL

from . import models

DEPENDENCIES = ["spi", "display"]

# Auto-load every file in models/ so they self-register into InkplateModel.models
for _mod in pkgutil.iter_modules(models.__path__):
    importlib.import_module(f".models.{_mod.name}", package=__package__)

MODELS = models.InkplateModel.models

inkplate_spi_ns = cg.esphome_ns.namespace("inkplate_spi")

Inkplate13     = inkplate_spi_ns.class_("Inkplate13",     display.Display, spi.SPIDevice)
Inkplate6Color = inkplate_spi_ns.class_("Inkplate6Color", display.Display, spi.SPIDevice)
Inkplate2      = inkplate_spi_ns.class_("Inkplate2",      display.Display, spi.SPIDevice)

_CPP_CLASSES = {
    "inkplate13":     Inkplate13,
    "inkplate6color": Inkplate6Color,
    "inkplate2":      Inkplate2,
}


def _set_model_id_type(config):
    config[CONF_ID].type = _CPP_CLASSES[config[CONF_MODEL]]
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Inkplate13),
            cv.Required(CONF_MODEL): cv.one_of(*MODELS, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(display.FULL_DISPLAY_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=False)),
    _set_model_id_type,
)


async def to_code(config):
    model = MODELS[config[CONF_MODEL]]
    var = cg.new_Pvariable(config[CONF_ID], model.width, model.height)

    init_bytes = model.get_init_bytes()
    bytes_str = ", ".join(f"0x{b:02X}" for b in init_bytes)
    cg.add(var.set_init_sequence(
        cg.RawExpression(f"std::vector<uint8_t>{{{bytes_str}}}")
    ))

    await display.register_display(var, config)
    await spi.register_spi_device(var, config)

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA],
            [(display.display_ns.class_("Display").operator("ref"), "it")],
            return_type=cg.void,
        )
        cg.add(var.set_writer(lambda_))
