#include "esphome/core/log.h"
#include "inkplate2.h"

namespace esphome::inkplate_spi {

static const char *TAG = "inkplate_spi";

void Inkplate2::setup() {}
void Inkplate2::loop() {}
void Inkplate2::update() {}
void Inkplate2::dump_config() { ESP_LOGCONFIG(TAG, "Inkplate 2"); }
void Inkplate2::draw_absolute_pixel_internal(int x, int y, Color color) {}

}  // namespace esphome::inkplate_spi
