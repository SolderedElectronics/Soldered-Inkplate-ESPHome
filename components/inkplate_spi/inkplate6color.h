#pragma once

#include "inkplate_spi.h"

namespace esphome::inkplate_spi {

class Inkplate6Color : public InkplateBase {
 public:
  Inkplate6Color(int width, int height) : InkplateBase(width, height) {}

  void dump_config() override;

  void set_pin_cs(int p) { pin_cs_ = p; }

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
  int pin_cs_{0};
};

}  // namespace esphome::inkplate_spi
