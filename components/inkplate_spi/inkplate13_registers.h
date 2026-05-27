#pragma once

#include <cstdint>

namespace esphome::inkplate_spi {

static constexpr uint8_t REG_PSR             = 0x00;
static constexpr uint8_t REG_PWR             = 0x01;
static constexpr uint8_t REG_POF             = 0x02;
static constexpr uint8_t REG_PON             = 0x04;
static constexpr uint8_t REG_BTST_N          = 0x05;
static constexpr uint8_t REG_BTST_P          = 0x06;
static constexpr uint8_t REG_DTM             = 0x10;
static constexpr uint8_t REG_DRF             = 0x12;
static constexpr uint8_t REG_PLL             = 0x30;
static constexpr uint8_t REG_CDI             = 0x50;
static constexpr uint8_t REG_TCON            = 0x60;
static constexpr uint8_t REG_TRES            = 0x61;
static constexpr uint8_t REG_PTLW            = 0x83;
static constexpr uint8_t REG_AN_TM           = 0x74;
static constexpr uint8_t REG_AGID            = 0x86;
static constexpr uint8_t REG_BUCK_BOOST_VDDN = 0xB0;
static constexpr uint8_t REG_TFT_VCOM_POWER  = 0xB1;
static constexpr uint8_t REG_EN_BUF          = 0xB6;
static constexpr uint8_t REG_BOOST_VDDP_EN   = 0xB7;
static constexpr uint8_t REG_CCSET           = 0xE0;
static constexpr uint8_t REG_PWS             = 0xE3;
static constexpr uint8_t REG_CMD66           = 0xF0;

static constexpr uint8_t REG_PSR_V[]             = {0xDF, 0x6B};
static constexpr uint8_t REG_PWR_V[]             = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
static constexpr uint8_t REG_POF_V[]             = {0x00};
static constexpr uint8_t REG_DRF_V[]             = {0x00};
static constexpr uint8_t REG_PLL_V[]             = {0x08};
static constexpr uint8_t REG_CDI_V[]             = {0xF7};
static constexpr uint8_t REG_TCON_V[]            = {0x03, 0x03};
static constexpr uint8_t REG_TRES_V[]            = {0x04, 0xB0, 0x03, 0x20};
static constexpr uint8_t REG_CMD66_V[]           = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
static constexpr uint8_t REG_EN_BUF_V[]          = {0x07};
static constexpr uint8_t REG_CCSET_V[]           = {0x01};
static constexpr uint8_t REG_PWS_V[]             = {0x22};
static constexpr uint8_t REG_AN_TM_V[]           = {0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55};
static constexpr uint8_t REG_AGID_V[]            = {0x10};
static constexpr uint8_t REG_BTST_P_V[]          = {0xD8, 0x18};
static constexpr uint8_t REG_BOOST_VDDP_EN_V[]   = {0x01};
static constexpr uint8_t REG_BTST_N_V[]          = {0xD8, 0x18};
static constexpr uint8_t REG_BUCK_BOOST_VDDN_V[] = {0x01};
static constexpr uint8_t REG_TFT_VCOM_POWER_V[]  = {0x02};

}  // namespace esphome::inkplate13
