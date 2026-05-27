#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/display/display_buffer.h"

#include <cinttypes>

namespace esphome::inkplate_spi {

class Inkplate13 : public display::DisplayBuffer,
                   public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                                         spi::CLOCK_POLARITY_LOW,
                                         spi::CLOCK_PHASE_LEADING,
                                         spi::DATA_RATE_10MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void update() override;

  void display(bool leave_on = true);
  void display_partial(int x, int y, int w, int h, bool leave_on = false);

  display::DisplayType get_display_type() override {
    return display::DisplayType::DISPLAY_TYPE_COLOR;
  }

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;

  int get_width_internal() override { return 1200; }
  int get_height_internal() override { return 1600; }

 private:
  enum ChipId {
    CHIP_MASTER = 1,
    CHIP_SLAVE  = 2,
    CHIP_BOTH   = CHIP_MASTER | CHIP_SLAVE,
  };

  void initialize_();
  void screen_init_();
  void set_panel_state_(bool state);
  bool set_panel_deep_sleep_(bool state);
  void set_io_();
  void set_panel_pins_to_low_();
  void reset_panel_();
  void wait_for_busy_();
  void send_command_(uint8_t cmd, const uint8_t *data, size_t len, ChipId chip);
  static uint8_t map_color_to_palette_(Color color);

  uint8_t *buffer_{nullptr};
  bool panel_state_{false};
};

}  // namespace esphome::inkplate_spi
