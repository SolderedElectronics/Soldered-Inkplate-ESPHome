from . import InkplateModel

CHIP_MASTER = 1
CHIP_SLAVE  = 2
CHIP_BOTH   = 3

InkplateModel(
    name="inkplate13",
    cpp_class="Inkplate13",
    width=1200,
    height=1600,
    init_sequence=[
        # (chip_target, cmd, data...)
        (CHIP_MASTER, 0x74, 0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55),  # AN_TM
        (CHIP_BOTH,   0xF0, 0x49, 0x55, 0x13, 0x5D, 0x05, 0x10),                    # CMD66
        (CHIP_BOTH,   0x00, 0xDF, 0x6B),                                               # PSR
        (CHIP_BOTH,   0x30, 0x08),                                                     # PLL
        (CHIP_BOTH,   0x50, 0xF7),                                                     # CDI
        (CHIP_BOTH,   0x60, 0x03, 0x03),                                               # TCON
        (CHIP_BOTH,   0x86, 0x10),                                                     # AGID
        (CHIP_BOTH,   0xE3, 0x22),                                                     # PWS
        (CHIP_BOTH,   0xE0, 0x01),                                                     # CCSET
        (CHIP_BOTH,   0x61, 0x04, 0xB0, 0x03, 0x20),                                  # TRES
        (CHIP_MASTER, 0x01, 0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38),                     # PWR
        (CHIP_MASTER, 0xB6, 0x07),                                                     # EN_BUF
        (CHIP_MASTER, 0x06, 0xD8, 0x18),                                               # BTST_P
        (CHIP_MASTER, 0xB7, 0x01),                                                     # BOOST_VDDP_EN
        (CHIP_MASTER, 0x05, 0xD8, 0x18),                                               # BTST_N
        (CHIP_MASTER, 0xB0, 0x01),                                                     # BUCK_BOOST_VDDN
        (CHIP_MASTER, 0xB1, 0x02),                                                     # TFT_VCOM_POWER
    ],
)
