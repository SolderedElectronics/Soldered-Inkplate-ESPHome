#include "esphome/core/log.h"
#include "inkplate6color.h"

namespace esphome::inkplate_spi {

static const char *TAG = "inkplate_spi";

void Inkplate6Color::setup() {}
void Inkplate6Color::loop() {}
void Inkplate6Color::update() {}
void Inkplate6Color::dump_config() { ESP_LOGCONFIG(TAG, "Inkplate 6 Color"); }
void Inkplate6Color::draw_absolute_pixel_internal(int x, int y, Color color) {}

}  // namespace esphome::inkplate_spi
