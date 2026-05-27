#pragma once

#include "inkplate_spi.h"

namespace esphome::inkplate_spi {

class Inkplate13 : public InkplateBase {
 public:
  Inkplate13(int width, int height) : InkplateBase(width, height) {}

  void dump_config() override;

  // Inkplate13-specific pin setters (dual-chip + bus-select pins)
  void set_pin_cs_m(int p) { pin_cs_m_ = p; }
  void set_pin_cs_s(int p) { pin_cs_s_ = p; }
  void set_pin_bs0(int p)  { pin_bs0_  = p; }
  void set_pin_bs1(int p)  { pin_bs1_  = p; }

  // Trigger a partial (subregion) update. x/y/w/h are in logical (rotated) coords.
  // The buffer must already contain the updated pixels before calling this.
  void display_partial(int x, int y, int w, int h) {
    start_partial_update_(x, y, w, h);
  }

 protected:
  // --- InkplateBase interface ---
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
  // Inkplate13-specific pin storage
  int pin_cs_m_{0};
  int pin_cs_s_{0};
  int pin_bs0_{0};
  int pin_bs1_{0};

  // Power-on sub-states (GPIO setup + delays + reset)
  enum PowerOnSub {
    PON_PINS_LOW,
    PON_PINS_LOW_WAIT,    // 50 ms
    PON_IO_WAIT,          // 100 ms after PWR_EN
    PON_RESET_LOW_WAIT,   // 100 ms RST low
    PON_RESET_HIGH_WAIT,  // 100 ms RST high
    PON_DONE,
  };

  // Transfer sub-states
  // Full path:    MASTER → WAIT_MASTER → SLAVE → WAIT_SLAVE → DONE
  // Partial path: PARTIAL_SETUP_M → PARTIAL_DATA_M → PARTIAL_WAIT_M
  //               → PARTIAL_SETUP_S → PARTIAL_DATA_S → PARTIAL_WAIT_S → DONE
  enum TransferSub {
    TRF_MASTER,
    TRF_WAIT_MASTER,
    TRF_SLAVE,
    TRF_WAIT_SLAVE,
    TRF_DONE,
    TRF_PARTIAL_SETUP_M,
    TRF_PARTIAL_DATA_M,
    TRF_PARTIAL_WAIT_M,
    TRF_PARTIAL_SETUP_S,
    TRF_PARTIAL_DATA_S,
    TRF_PARTIAL_WAIT_S,
  };

  // Power-off sub-states (POF → busy → release pins)
  enum PowerOffSub {
    POFF_SEND,
    POFF_WAIT,
    POFF_RELEASE,
    POFF_DONE,
  };

  PowerOnSub  pon_sub_{PON_PINS_LOW};
  TransferSub trf_sub_{TRF_MASTER};
  PowerOffSub poff_sub_{POFF_SEND};
  uint32_t    sub_start_ms_{0};
  size_t      trf_row_{0};  // current row in chunked transfer

  // Per-chip PTLW parameters computed once per partial update cycle.
  struct PartialChipParams {
    bool    needed{false};
    uint8_t ptlw[9]{};     // 9-byte PTLW payload
    int     mem_col_off{0}; // byte offset from row start in buffer_
    int     bytes_per_row{0};
    int     row_start{0};
    int     row_end{0};
  };

  PartialChipParams ptlw_master_;
  PartialChipParams ptlw_slave_;

  void set_io_pins_();
  void set_all_pins_low_();
  void compute_ptlw_params_();  // fills ptlw_master_ / ptlw_slave_ from partial_x/y/w/h
};

}  // namespace esphome::inkplate_spi
