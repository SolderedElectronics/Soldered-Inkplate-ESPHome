from . import InkplateModel

InkplateModel(
    name="inkplate2",
    cpp_class="Inkplate2",
    width=104,
    height=212,
    spi_data_rate=1_000_000,
    pins={
        "rst":  19,
        "dc":   33,
        "busy": 32,
        "cs":   27,
    },
    # init_sequence intentionally empty: PON (0x04) must precede panel settings,
    # but our state machine sends INIT before PON. Full init handled in
    # do_power_on_step_() sub-states to match the Arduino order.
)
