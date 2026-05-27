#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/display/display_buffer.h"

#include <cinttypes>
#include <vector>

namespace esphome::inkplate_spi {

class Inkplate6Color : public display::DisplayBuffer,
                       public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST,
                                             spi::CLOCK_POLARITY_LOW,
                                             spi::CLOCK_PHASE_LEADING,
                                             spi::DATA_RATE_10MHZ> {
 public:
  Inkplate6Color(int width, int height) : width_(width), height_(height) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  void update() override;

  void set_init_sequence(std::vector<uint8_t> seq) { init_seq_ = std::move(seq); }

  display::DisplayType get_display_type() override {
    return display::DisplayType::DISPLAY_TYPE_COLOR;
  }

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;

  int get_width_internal() override { return width_; }
  int get_height_internal() override { return height_; }

 private:
  int width_{0};
  int height_{0};
  uint8_t *buffer_{nullptr};
  std::vector<uint8_t> init_seq_;
};

}  // namespace esphome::inkplate_spi
