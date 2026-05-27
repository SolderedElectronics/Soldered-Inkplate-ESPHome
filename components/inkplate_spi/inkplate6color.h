#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/display/display_buffer.h"

#include <cinttypes>

namespace esphome::inkplate_spi {

class Inkplate6Color : public display::DisplayBuffer,
                       public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                                             spi::CLOCK_POLARITY_LOW,
                                             spi::CLOCK_PHASE_LEADING,
                                             spi::DATA_RATE_10MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void update() override;

  display::DisplayType get_display_type() override {
    return display::DisplayType::DISPLAY_TYPE_COLOR;
  }

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;

  int get_width_internal() override { return 0; }   // TODO
  int get_height_internal() override { return 0; }  // TODO

 private:
  uint8_t *buffer_{nullptr};
};

}  // namespace esphome::inkplate_spi
