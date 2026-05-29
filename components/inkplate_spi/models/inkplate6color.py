from . import InkplateModel

InkplateModel(
    name="inkplate6color",
    cpp_class="Inkplate6Color",
    width=600,
    height=448,
    spi_data_rate=2_000_000,
    pins={
        "rst": 19,
        "dc": 33,
        "busy": 32,
        "cs": 27,
    },
    init_sequence=[
        # (chip, cmd, data...) — chip target is ignored (single chip)
        (1, 0x00, 0xEF, 0x08),                # PANEL_SET
        (1, 0x01, 0x37, 0x00, 0x05, 0x05),    # POWER_SET
        (1, 0x03, 0x00),                      # POWER_OFF_SEQ_SET
        (1, 0x06, 0xC7, 0xC7, 0x1D),          # BOOSTER_SOFTSTART
        (1, 0x41, 0x00),                      # TEMP_SENSOR_EN
        (1, 0x50, 0x37),                      # VCOM/CDI
        (1, 0x60, 0x20),
        (1, 0x61, 0x02, 0x58, 0x01, 0xC0),    # RESOLUTION (600=0x258, 448=0x1C0)
        (1, 0xE3, 0xAA),                      # PWS
    ],
)
