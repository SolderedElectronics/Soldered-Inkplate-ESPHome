#include "esphome/core/log.h"
#include "inkplate13.h"

namespace esphome::inkplate13 {

static const char *TAG = "inkplate13.component";

void Inkplate13::setup() {
  this->spi_setup(); // Required to initialize this SPI device
}

void Inkplate13::loop() {
  
}

void Inkplate13::dump_config() {
  ESP_LOGCONFIG(TAG, "Inkplate 13");
}

}
