import pkgutil
import importlib

import esphome.config_validation as cv
from esphome.config_validation import update_interval
import esphome.codegen as cg
from esphome import pins as esphome_pins
from esphome.components import spi, display
from esphome.const import (
    CONF_FULL_UPDATE_EVERY,
    CONF_ID,
    CONF_LAMBDA,
    CONF_MODEL,
    CONF_PAGES,
    CONF_UPDATE_INTERVAL,
)

from . import models

DEPENDENCIES = ["spi"]

# ID used to name the generated `static const uint8_t[]` init sequence array in C++.
# ESPHome codegen requires a declared ID to emit a named static array via
# cg.static_const_array(); the array itself is only emitted when init_bytes is non-empty.
CONF_INIT_SEQUENCE_ID = "init_sequence_id"

CONF_PIN_RST    = "pin_rst"
CONF_PIN_DC     = "pin_dc"
CONF_PIN_BUSY   = "pin_busy"
CONF_PIN_PWR_EN = "pin_pwr_en"
CONF_PIN_CS_M   = "pin_cs_m"
CONF_PIN_CS_S   = "pin_cs_s"
CONF_PIN_BS0    = "pin_bs0"
CONF_PIN_BS1    = "pin_bs1"
CONF_PIN_CS     = "pin_cs"

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

# Maps model pin name → (config key, pin schema validator)
_PIN_CONF_MAP = {
    "rst":    (CONF_PIN_RST,    esphome_pins.gpio_output_pin_schema),
    "dc":     (CONF_PIN_DC,     esphome_pins.gpio_output_pin_schema),
    "busy":   (CONF_PIN_BUSY,   esphome_pins.gpio_input_pin_schema),
    "pwr_en": (CONF_PIN_PWR_EN, esphome_pins.gpio_output_pin_schema),
    "cs_m":   (CONF_PIN_CS_M,   esphome_pins.gpio_output_pin_schema),
    "cs_s":   (CONF_PIN_CS_S,   esphome_pins.gpio_output_pin_schema),
    "bs0":    (CONF_PIN_BS0,    esphome_pins.gpio_output_pin_schema),
    "bs1":    (CONF_PIN_BS1,    esphome_pins.gpio_output_pin_schema),
    "cs":     (CONF_PIN_CS,     esphome_pins.gpio_output_pin_schema),
}


def _set_model_id_type(config):
    # CONFIG_SCHEMA must declare the ID with a concrete C++ type up front, so it
    # uses Inkplate13 as a placeholder. This validator replaces that placeholder
    # with the correct class for the selected model before codegen runs.
    config[CONF_ID].type = _CPP_CLASSES[config[CONF_MODEL]]
    return config


def _inject_model_pins(config):
    """Fill in pin defaults from model for any pins not specified by the user."""
    model = MODELS[config[CONF_MODEL]]
    for name, num in model.pins.items():
        conf_key, schema = _PIN_CONF_MAP[name]
        if conf_key not in config:
            config[conf_key] = schema({"number": num})
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
            cv.GenerateID(): cv.declare_id(Inkplate13),  # placeholder — overwritten by _set_model_id_type
            cv.GenerateID(CONF_INIT_SEQUENCE_ID): cv.declare_id(cg.uint8),
            cv.Required(CONF_MODEL): cv.one_of(*MODELS, lower=True),
            cv.Optional(CONF_FULL_UPDATE_EVERY, default=1): cv.positive_int,
            # Pin overrides — optional, defaults injected from model by _inject_model_pins
            cv.Optional(CONF_PIN_RST):    esphome_pins.gpio_output_pin_schema,
            cv.Optional(CONF_PIN_DC):     esphome_pins.gpio_output_pin_schema,
            cv.Optional(CONF_PIN_BUSY):   esphome_pins.gpio_input_pin_schema,
            cv.Optional(CONF_PIN_PWR_EN): esphome_pins.gpio_output_pin_schema,
            cv.Optional(CONF_PIN_CS_M):   esphome_pins.gpio_output_pin_schema,
            cv.Optional(CONF_PIN_CS_S):   esphome_pins.gpio_output_pin_schema,
            cv.Optional(CONF_PIN_BS0):    esphome_pins.gpio_output_pin_schema,
            cv.Optional(CONF_PIN_BS1):    esphome_pins.gpio_output_pin_schema,
            cv.Optional(CONF_PIN_CS):     esphome_pins.gpio_output_pin_schema,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(display.FULL_DISPLAY_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=False)),
    _set_model_id_type,
    _inject_model_pins,
)


async def to_code(config):
    model = MODELS[config[CONF_MODEL]]
    var = cg.new_Pvariable(config[CONF_ID], model.width, model.height)

    init_bytes = model.get_init_bytes()
    if init_bytes:
        init_seq_arr = cg.static_const_array(config[CONF_INIT_SEQUENCE_ID], init_bytes)
        cg.add(var.set_init_sequence(init_seq_arr, len(init_bytes)))

    cg.add(var.set_full_update_every(config[CONF_FULL_UPDATE_EVERY]))
    cg.add(var.set_data_rate(model.spi_data_rate))

    # Output pins
    for conf_key in [CONF_PIN_RST, CONF_PIN_DC, CONF_PIN_PWR_EN,
                     CONF_PIN_CS_M, CONF_PIN_CS_S, CONF_PIN_BS0, CONF_PIN_BS1, CONF_PIN_CS]:
        if conf_key not in config:
            continue
        pin = await cg.gpio_pin_expression(config[conf_key])
        cg.add(getattr(var, f"set_{conf_key}")(pin))

    # Input pin
    if CONF_PIN_BUSY in config:
        pin = await cg.gpio_pin_expression(config[CONF_PIN_BUSY])
        cg.add(var.set_pin_busy(pin))

    await display.register_display(var, config)
    await spi.register_spi_device(var, config, write_only=True)

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA],
            [(display.display_ns.class_("Display").operator("ref"), "it")],
            return_type=cg.void,
        )
        cg.add(var.set_writer(lambda_))
