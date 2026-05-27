import pkgutil
import importlib

import esphome.config_validation as cv
from esphome.config_validation import update_interval
import esphome.codegen as cg
from esphome.components import spi, display
from esphome.const import CONF_ID, CONF_LAMBDA, CONF_MODEL, CONF_PAGES, CONF_UPDATE_INTERVAL

from . import models

DEPENDENCIES = ["spi", "display"]

CONF_FULL_UPDATE_EVERY = "full_update_every"

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


def _final_validate(config):
    # SPI bus validation (no MISO needed — display is write-only)
    spi.final_validate_device_schema(
        "inkplate_spi", require_miso=False, require_mosi=True
    )(config)

    # Auto-set update_interval to 1 min when user has a lambda but didn't specify interval
    if CONF_LAMBDA in config or CONF_PAGES in config:
        if CONF_UPDATE_INTERVAL not in config:
            config[CONF_UPDATE_INTERVAL] = update_interval("1min")

    # Enforce panel minimum update interval
    model = MODELS.get(config.get(CONF_MODEL))
    if model is not None:
        min_ms = model.min_update_interval_ms
        if min_ms > 0:
            interval = config.get(CONF_UPDATE_INTERVAL)
            if interval is not None and interval.total_milliseconds < min_ms:
                raise cv.Invalid(
                    f"update_interval {interval.total_milliseconds}ms is below minimum {min_ms}ms "
                    f"for {model.name} — panel may be damaged by too-frequent refreshes"
                )

    return config


FINAL_VALIDATE_SCHEMA = _final_validate


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Inkplate13),
            cv.Required(CONF_MODEL): cv.one_of(*MODELS, lower=True),
            cv.Optional(CONF_FULL_UPDATE_EVERY, default=1): cv.positive_int,
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

    cg.add(var.set_full_update_every(config[CONF_FULL_UPDATE_EVERY]))

    # Common pins (all boards)
    pins = model.pins
    cg.add(var.set_pin_rst(pins["rst"]))
    cg.add(var.set_pin_dc(pins["dc"]))
    cg.add(var.set_pin_busy(pins["busy"]))
    cg.add(var.set_pin_pwr_en(pins["pwr_en"]))

    # Board-specific pins (present only if in model's pins dict)
    if "cs_m" in pins:
        cg.add(var.set_pin_cs_m(pins["cs_m"]))
    if "cs_s" in pins:
        cg.add(var.set_pin_cs_s(pins["cs_s"]))
    if "bs0" in pins:
        cg.add(var.set_pin_bs0(pins["bs0"]))
    if "bs1" in pins:
        cg.add(var.set_pin_bs1(pins["bs1"]))
    if "cs" in pins:
        cg.add(var.set_pin_cs(pins["cs"]))

    await display.register_display(var, config)
    await spi.register_spi_device(var, config, write_only=True)

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA],
            [(display.display_ns.class_("Display").operator("ref"), "it")],
            return_type=cg.void,
        )
        cg.add(var.set_writer(lambda_))
