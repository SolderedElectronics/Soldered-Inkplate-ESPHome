from . import InkplateModel

InkplateModel(
    name="inkplate7spectra",
    cpp_class="Inkplate7Spectra",
    width=800,
    height=480,
    spi_data_rate=8_000_000,
    min_update_interval_ms=30000,  # ACeP panel — 30 s minimum between refreshes
    pins={
        "rst": 4,
        "dc": 14,
        "busy": 7,
        "pwr_en": 21,
        "cs": 42,
        "bs0": 6,
        "bs1": 5,
    },
    # E Ink E6_73 sample code V1.2 EPD_Init(), same order and values.
    init_sequence=[
        # (chip, cmd, data...) — chip target is ignored (single chip)
        (1, 0xAA, 0x49, 0x55, 0x20, 0x08, 0x09, 0x18),  # CMDH
        (1, 0x01, 0x3F),                                # PWR
        (1, 0x00, 0x5F, 0x69),                          # PSR
        (1, 0x05, 0x40, 0x1F, 0x1F, 0x2C),              # BTST1
        (1, 0x08, 0x6F, 0x1F, 0x1F, 0x22),              # BTST3
        (1, 0x06, 0x6F, 0x1F, 0x17, 0x17),              # BTST2 first setting
        (1, 0x03, 0x00, 0x54, 0x00, 0x44),              # PFS
        (1, 0x60, 0x02, 0x00),                          # TCON
        (1, 0x30, 0x08),                                # PLL (required for version 2 IC)
        (1, 0x50, 0x3F),                                # CDI
        (1, 0x61, 0x03, 0x20, 0x01, 0xE0),              # TRES (800x480)
        (1, 0xE3, 0x2F),                                # PWS
        (1, 0x84, 0x01),                                # T_VDCS
    ],
)
