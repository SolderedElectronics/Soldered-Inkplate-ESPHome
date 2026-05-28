#pragma once

#include "inkplate_spi.h"

namespace esphome::inkplate_spi {

// Inkplate6Color — driver for the 6" 7-color ACeP panel.
//
// Single-chip, single CS pin. EPD controller connected directly to ESP32 GPIO.
// No partial update support (ACeP single-chip panels don't support PTLW).
//
// Transfer sequence differs from Inkplate13: PON (0x04) is sent AFTER pixel
// data, not before. Power-off wait is BUSY LOW (not HIGH like Inkplate13).
class Inkplate6Color : public InkplateBase {
 public:
  Inkplate6Color(int width, int height) : InkplateBase(width, height) {}

  void dump_config() override;

  void set_pin_cs(GPIOPin *p) { pin_cs_ = p; }

 protected:
  uint8_t map_color_to_index_(Color color) override;
  void    send_command_to_chip_(uint8_t cmd, const uint8_t *data, size_t len, uint8_t chip) override;
  void    prepare_for_update_() override;

  bool do_power_on_step_()  override;
  void do_send_pon_()       override;
  bool do_transfer_step_()  override;
  void do_send_refresh_()   override;
  bool do_power_off_step_() override;
  bool is_busy_()           override;
  void do_deep_sleep_()     override;
  void do_emergency_off_()  override;

 private:
  GPIOPin *pin_cs_{nullptr};

  // Power-on sub-states: GPIO setup → 100ms pre-wait → RST pulse → BUSY wait
  enum PowerOnSub {
    PON_SETUP,
    PON_PRE_WAIT,       // 100ms — RST held low before reset pulse
    PON_RST_LOW_WAIT,   // 10ms RST low (reset pulse)
    PON_RST_HIGH_WAIT,  // 200ms RST high (panel initialising)
    PON_WAIT_BUSY,      // wait BUSY HIGH (panel ready)
    PON_DONE,
  };

  // Transfer sub-states: resolution → DTM stream → PON cmd → BUSY wait
  // PON (0x04) is sent after pixel data on this controller (unlike Inkplate13).
  enum TransferSub {
    TRF_SEND_RESOLUTION,
    TRF_STREAM_DATA,
    TRF_SEND_PON,
    TRF_WAIT_PON_BUSY,
    TRF_DONE,
  };

  // Power-off sub-states: POF → wait BUSY LOW → 200ms
  // BUSY goes LOW when power-off completes (opposite condition from Inkplate13).
  enum PowerOffSub {
    POFF_SEND,
    POFF_WAIT,
    POFF_DELAY,
    POFF_DONE,
  };

  PowerOnSub  pon_sub_{PON_SETUP};
  TransferSub trf_sub_{TRF_SEND_RESOLUTION};
  PowerOffSub poff_sub_{POFF_SEND};
  uint32_t    sub_start_ms_{0};
  size_t      trf_row_{0};
};

}  // namespace esphome::inkplate_spi
