#pragma once

#include "inkplate_spi.h"

namespace esphome::inkplate_spi {

// Inkplate7Spectra — driver for the 7.3" 800x480 Spectra 6 panel (E Ink E6_73).
//
// Single chip, 4-wire SPI (BS0/BS1 low). Init values and refresh sequence follow
// the E Ink E6_73 sample code V1.2 and the Arduino Inkplate7SPECTRADriver:
//   init → DTM + data → PON → BTST2 second setting → DRF → POF → 15 ms
//   → PSR VDD-off setting → release pins → PWR_EN low.
// PON is sent after the pixel data, so do_send_pon_() is a no-op.
class Inkplate7Spectra : public InkplateBase {
 public:
  Inkplate7Spectra(int width, int height) : InkplateBase(width, height) {}

  void dump_config() override;

  void set_pin_cs(GPIOPin *p)  { this->pin_cs_  = p; }
  void set_pin_bs0(GPIOPin *p) { this->pin_bs0_ = p; }
  void set_pin_bs1(GPIOPin *p) { this->pin_bs1_ = p; }

 protected:
  static constexpr size_t ROWS_PER_CHUNK = 16;

  enum PowerOnSub {
    PON_PINS_LOW,
    PON_PINS_LOW_WAIT,  // 50 ms
    PON_IO_WAIT,        // 100 ms after PWR_EN
    PON_RST_1_LOW,      // dual reset, each phase 35 ms (>= 30 ms required)
    PON_RST_1_HIGH,
    PON_RST_2_LOW,
    PON_WAIT_BUSY,
    PON_SETTLE,         // 10 ms after busy released
    PON_DONE,
  };

  enum TransferSub {
    TRF_STREAM,
    TRF_WAIT_PON,
    TRF_DONE,
  };

  enum PowerOffSub {
    POFF_SEND,
    POFF_WAIT,
    POFF_DELAY,         // >= 10 ms after POF
    POFF_PSR_WAIT,
    POFF_DONE,
  };

  uint8_t map_color_to_index_(Color color) override;
  void    send_command_to_chip_(uint8_t cmd, const uint8_t *data, size_t len, uint8_t chip) override;
  void    prepare_for_update_() override;
  bool do_power_on_step_()  override;
  bool do_send_pon_()       override;
  bool do_transfer_step_()  override;
  void do_send_refresh_()   override;
  bool do_power_off_step_() override;
  bool is_busy_()           override;
  void do_emergency_off_()  override;

  void set_all_pins_low_();
  void set_io_pins_();

  GPIOPin *pin_cs_{nullptr};
  GPIOPin *pin_bs0_{nullptr};
  GPIOPin *pin_bs1_{nullptr};

  PowerOnSub  pon_sub_{PON_PINS_LOW};
  TransferSub trf_sub_{TRF_STREAM};
  PowerOffSub poff_sub_{POFF_SEND};
  uint32_t    sub_start_ms_{0};
  uint32_t    last_cmd_ms_{0};
  size_t      trf_row_{0};
};

}  // namespace esphome::inkplate_spi
