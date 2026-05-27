#include "esphome/core/log.h"
#include "inkplate2.h"

namespace esphome::inkplate_spi {

static const char *TAG = "inkplate2";

void Inkplate2::dump_config() {
  ESP_LOGCONFIG(TAG, "Inkplate 2");
}

uint8_t Inkplate2::map_color_to_index_(Color color) {
  // TODO: map to panel palette
  return 0;
}

void Inkplate2::send_command_to_chip_(uint8_t cmd, const uint8_t *data, size_t len, uint8_t chip) {
  // TODO: implement SPI command send
}

void Inkplate2::prepare_for_update_() {
  // TODO: reset per-cycle sub-states
}

bool Inkplate2::do_power_on_step_() {
  // TODO: GPIO init + RST pulse
  return true;  // done immediately — stub
}

void Inkplate2::do_send_pon_() {
  // TODO: send PON command
}

bool Inkplate2::do_transfer_step_() {
  // TODO: push buffer to panel row-by-row
  return true;  // done immediately — stub
}

void Inkplate2::do_send_refresh_() {
  // TODO: send DRF command
}

bool Inkplate2::do_power_off_step_() {
  // TODO: send POF + busy wait
  return true;  // done immediately — stub
}

bool Inkplate2::is_busy_() {
  // TODO: poll busy pin (return true = panel ready / not busy)
  return true;
}

void Inkplate2::do_deep_sleep_() {
  // TODO: drive RST low
}

void Inkplate2::do_emergency_off_() {
  // TODO: kill pwr_en + RST low
}

}  // namespace esphome::inkplate_spi
