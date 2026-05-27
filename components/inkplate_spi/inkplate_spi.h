#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/display/display_buffer.h"

#include <cinttypes>
#include <vector>

namespace esphome::inkplate_spi {

class InkplateSPIBase : public display::DisplayBuffer,
                        public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                                              spi::CLOCK_POLARITY_LOW,
                                              spi::CLOCK_PHASE_LEADING,
                                              spi::DATA_RATE_10MHZ> {
 public:
  InkplateSPIBase(int width, int height) : width_(width), height_(height) {}

  void setup()       override;
  void loop()        override;
  void dump_config() override;
  void update()      override;

  // Called by ESPHome before OTA / reboot.
  void on_safe_shutdown() override;

  void set_init_sequence(std::vector<uint8_t> seq) { init_seq_ = std::move(seq); }
  void set_full_update_every(int n) { full_update_every_ = n; }

  void set_pin_rst(int p)    { pin_rst_    = p; }
  void set_pin_dc(int p)     { pin_dc_     = p; }
  void set_pin_busy(int p)   { pin_busy_   = p; }
  void set_pin_pwr_en(int p) { pin_pwr_en_ = p; }

  display::DisplayType get_display_type() override {
    return display::DisplayType::DISPLAY_TYPE_COLOR;
  }

  // Returns true while a full or partial update is in progress.
  bool is_busy() const { return state_ != STATE_IDLE; }

 protected:
  int get_width_internal()  override { return width_; }
  int get_height_internal() override { return height_; }

  void draw_absolute_pixel_internal(int x, int y, Color color) override;

  // --- virtual hardware interface ---

  // Map a Color to a 4-bit palette index (panel-specific).
  virtual uint8_t map_color_to_index_(Color color) = 0;

  // Send a command + data to a specific chip target.
  // chip: bitmask, 1=master/primary, 2=slave/secondary, 3=both.
  // Single-chip boards can ignore the chip parameter.
  virtual void send_command_to_chip_(uint8_t cmd, const uint8_t *data, size_t len, uint8_t chip) = 0;

  // Called once before STATE_POWER_ON each cycle — reset per-cycle sub-states.
  virtual void prepare_for_update_() {}

  // Power-on sequence (GPIO setup, delays, reset).
  // Called every loop() tick during STATE_POWER_ON; return true when done.
  virtual bool do_power_on_step_() = 0;

  // Replay init_seq_ via send_command_to_chip_().
  // Default implementation in base handles this; subclass may override.
  virtual void do_init_();

  // Send power-on command (PON).
  virtual void do_send_pon_() = 0;

  // Send data to panel.
  // Called every loop() tick during STATE_TRANSFER; return true when done.
  virtual bool do_transfer_step_() = 0;

  // Send display-refresh command (DRF / trigger).
  virtual void do_send_refresh_() = 0;

  // Power-off + pin release sequence.
  // Called every loop() tick during STATE_POWER_OFF; return true when done.
  virtual bool do_power_off_step_() = 0;

  // Non-blocking BUSY pin read. Return true when panel is ready (not busy).
  virtual bool is_busy_() = 0;

  // Enter panel deep sleep (called once after STATE_POWER_OFF completes).
  // Default: no-op. Override to drive RST low or send deep-sleep command.
  virtual void do_deep_sleep_() {}

  // Emergency hardware power-off called by on_safe_shutdown() if mid-refresh.
  // Default: no-op. Override to cut pwr_en / drive RST low immediately.
  virtual void do_emergency_off_() {}

  // Trigger a partial (subregion) update. Stores region, sets partial_ = true,
  // then kicks off STATE_POWER_ON. Buffer must already contain updated pixels.
  void start_partial_update_(int x, int y, int w, int h);

  // --- shared data ---
  int      width_{0};
  int      height_{0};
  int      pin_rst_{0};
  int      pin_dc_{0};
  int      pin_busy_{0};
  int      pin_pwr_en_{0};
  int      full_update_every_{1};
  int      update_count_{0};
  bool     partial_{false};   // true during a display_partial() cycle
  int      partial_x_{0};
  int      partial_y_{0};
  int      partial_w_{0};
  int      partial_h_{0};
  uint8_t *buffer_{nullptr};
  std::vector<uint8_t> init_seq_;

 private:
  enum State {
    STATE_IDLE,
    STATE_POWER_ON,
    STATE_INIT,
    STATE_PON,
    STATE_WAIT_PON,
    STATE_TRANSFER,
    STATE_REFRESH,
    STATE_WAIT_REFRESH,
    STATE_POWER_OFF,
    STATE_DEEP_SLEEP,
  };

  void set_state_(State s);
  void process_state_();

  State    state_{STATE_IDLE};
  uint32_t state_start_ms_{0};
};

}  // namespace esphome::inkplate_spi
