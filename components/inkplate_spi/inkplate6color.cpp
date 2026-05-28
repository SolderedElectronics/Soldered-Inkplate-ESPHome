#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "inkplate6color.h"

namespace esphome::inkplate_spi {

static const char *TAG = "inkplate6color";

static constexpr uint8_t REG_DTM   = 0x10;
static constexpr uint8_t REG_DRF   = 0x12;
static constexpr uint8_t REG_PON   = 0x04;
static constexpr uint8_t REG_POF   = 0x02;
static constexpr uint8_t REG_SLEEP = 0x07;

// ---------------------------------------------------------------------------

void Inkplate6Color::dump_config() {
  ESP_LOGCONFIG(TAG, "Inkplate 6 Color %dx%d", width_, height_);
}

// ---------------------------------------------------------------------------
// Color mapping — 7-color ACeP palette
// Indices differ from Inkplate13: BLACK=0 WHITE=1 GREEN=2 BLUE=3 RED=4 YELLOW=5 ORANGE=6
// ---------------------------------------------------------------------------

uint8_t Inkplate6Color::map_color_to_index_(Color color) {
  uint8_t r = color.red, g = color.green, b = color.blue;
  if (r > 200 && g > 200 && b > 200) return 1;  // WHITE
  if (r < 50  && g < 50  && b < 50 ) return 0;  // BLACK
  if (r > 150 && g < 100 && b < 100) return 4;  // RED
  if (r < 100 && g > 150 && b < 100) return 2;  // GREEN
  if (r < 100 && g < 100 && b > 150) return 3;  // BLUE
  if (r > 150 && g > 150 && b < 100) return 5;  // YELLOW
  if (r > 150 && g > 80  && b < 80 ) return 6;  // ORANGE
  return 0;
}

// ---------------------------------------------------------------------------
// SPI — 4-wire with DC pin.
// Two separate CS transactions: command byte (DC low), data bytes (DC high).
// Faithful to the Arduino reference which deasserts CS between cmd and data.
// ---------------------------------------------------------------------------

void Inkplate6Color::send_command_to_chip_(uint8_t cmd, const uint8_t *data, size_t len, uint8_t /*chip*/) {
  pin_cs_->digital_write(false);
  pin_dc_->digital_write(false);
  this->enable();
  this->write_byte(cmd);
  this->disable();
  pin_cs_->digital_write(true);

  if (len > 0 && data != nullptr) {
    pin_cs_->digital_write(false);
    pin_dc_->digital_write(true);
    this->enable();
    this->write_array(data, len);
    this->disable();
    pin_cs_->digital_write(true);
  }
}

// ---------------------------------------------------------------------------
// Prepare
// ---------------------------------------------------------------------------

void Inkplate6Color::prepare_for_update_() {
  pon_sub_      = PON_SETUP;
  trf_sub_      = TRF_SEND_RESOLUTION;
  poff_sub_     = POFF_SEND;
  sub_start_ms_ = 0;
  trf_row_      = 0;
}

// ---------------------------------------------------------------------------
// Power-on sub-state machine
// ---------------------------------------------------------------------------

bool Inkplate6Color::do_power_on_step_() {
  uint32_t now = App.get_loop_component_start_time();
  switch (pon_sub_) {

    case PON_SETUP:
      pin_rst_->pin_mode(gpio::FLAG_OUTPUT);
      pin_dc_->pin_mode(gpio::FLAG_OUTPUT);
      pin_cs_->pin_mode(gpio::FLAG_OUTPUT);
      pin_busy_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
      pin_dc_->digital_write(true);    // idle high
      pin_cs_->digital_write(true);    // deselected
      pin_rst_->digital_write(false);  // hold RST low during pre-wait
      sub_start_ms_ = now;
      pon_sub_ = PON_PRE_WAIT;
      return false;

    case PON_PRE_WAIT:
      if (now - sub_start_ms_ < 100) return false;
      sub_start_ms_ = now;
      pon_sub_ = PON_RST_LOW_WAIT;
      return false;

    case PON_RST_LOW_WAIT:
      if (now - sub_start_ms_ < 10) return false;
      pin_rst_->digital_write(true);
      sub_start_ms_ = now;
      pon_sub_ = PON_RST_HIGH_WAIT;
      return false;

    case PON_RST_HIGH_WAIT:
      if (now - sub_start_ms_ < 200) return false;
      pon_sub_ = PON_WAIT_BUSY;
      return false;

    case PON_WAIT_BUSY:
      if (!is_busy_()) return false;  // wait BUSY HIGH — panel ready after reset
      pon_sub_ = PON_DONE;
      return true;

    case PON_DONE:
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// PON — 100ms settling required after init sequence, then second CDI write.
// Blocking delay acceptable here: runs once per cycle, well within WDT limit.
// ---------------------------------------------------------------------------

void Inkplate6Color::do_send_pon_() {
  delay(100);
  const uint8_t v = 0x37;
  send_command_to_chip_(0x50, &v, 1, 1);
}

// ---------------------------------------------------------------------------
// Transfer sub-state machine
// Sends resolution, streams pixel data, then sends PON and waits BUSY HIGH.
// ---------------------------------------------------------------------------

bool Inkplate6Color::do_transfer_step_() {
  // 8 rows × 300 bytes = 2400 bytes per tick (~9.6ms at 2MHz, ~0.19ms at 10MHz)
  static constexpr size_t ROWS_PER_CHUNK = 8;
  const size_t rows          = (size_t) height_;
  const size_t bytes_per_row = (size_t) width_ / 2;

  switch (trf_sub_) {

    case TRF_SEND_RESOLUTION: {
      const uint8_t res[] = {0x02, 0x58, 0x01, 0xC0};
      send_command_to_chip_(0x61, res, 4, 1);

      // Send DTM command byte as its own CS transaction (DC low)
      pin_cs_->digital_write(false);
      pin_dc_->digital_write(false);
      this->enable();
      this->write_byte(REG_DTM);
      this->disable();
      pin_cs_->digital_write(true);

      // Open data transaction (DC high, CS low) — held open across ticks
      pin_cs_->digital_write(false);
      pin_dc_->digital_write(true);
      this->enable();
      trf_row_ = 0;
      trf_sub_ = TRF_STREAM_DATA;
      ESP_LOGD(TAG, "transfer: DTM start");
      return false;
    }

    case TRF_STREAM_DATA: {
      size_t end = std::min(trf_row_ + ROWS_PER_CHUNK, rows);
      for (size_t i = trf_row_; i < end; i++)
        this->write_array(buffer_ + i * bytes_per_row, bytes_per_row);
      trf_row_ = end;
      if (trf_row_ >= rows) {
        this->disable();
        pin_cs_->digital_write(true);
        trf_sub_ = TRF_SEND_PON;
        ESP_LOGD(TAG, "transfer: DTM done");
      }
      return false;
    }

    case TRF_SEND_PON:
      send_command_to_chip_(REG_PON, nullptr, 0, 1);
      trf_sub_ = TRF_WAIT_PON_BUSY;
      return false;

    case TRF_WAIT_PON_BUSY:
      if (!is_busy_()) return false;  // wait BUSY HIGH
      trf_sub_ = TRF_DONE;
      return true;

    case TRF_DONE:
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void Inkplate6Color::do_send_refresh_() {
  send_command_to_chip_(REG_DRF, nullptr, 0, 1);
}

// ---------------------------------------------------------------------------
// Power-off sub-state machine
// After POF, BUSY goes LOW when power-off completes — opposite of Inkplate13.
// ---------------------------------------------------------------------------

bool Inkplate6Color::do_power_off_step_() {
  uint32_t now = App.get_loop_component_start_time();
  switch (poff_sub_) {

    case POFF_SEND:
      send_command_to_chip_(REG_POF, nullptr, 0, 1);
      poff_sub_ = POFF_WAIT;
      return false;

    case POFF_WAIT:
      if (is_busy_()) return false;  // BUSY still HIGH — wait for it to go LOW
      sub_start_ms_ = now;
      poff_sub_ = POFF_DELAY;
      return false;

    case POFF_DELAY:
      if (now - sub_start_ms_ < 200) return false;
      poff_sub_ = POFF_DONE;
      return true;

    case POFF_DONE:
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Busy pin — HIGH = ready, LOW = busy
// ---------------------------------------------------------------------------

bool Inkplate6Color::is_busy_() {
  return pin_busy_->digital_read();
}

// ---------------------------------------------------------------------------
// Deep sleep
// ---------------------------------------------------------------------------

void Inkplate6Color::do_deep_sleep_() {
  const uint8_t v = 0xA5;
  delay(10);
  send_command_to_chip_(REG_SLEEP, &v, 1, 1);
  delay(100);
  pin_dc_->pin_mode(gpio::FLAG_INPUT);
  pin_cs_->pin_mode(gpio::FLAG_INPUT);
  pin_busy_->pin_mode(gpio::FLAG_INPUT);
  pin_rst_->pin_mode(gpio::FLAG_OUTPUT);
  pin_rst_->digital_write(false);
  ESP_LOGD(TAG, "panel deep sleep");
}

void Inkplate6Color::do_emergency_off_() {
  pin_rst_->pin_mode(gpio::FLAG_OUTPUT);
  pin_rst_->digital_write(false);
  pin_cs_->pin_mode(gpio::FLAG_OUTPUT);
  pin_cs_->digital_write(true);
  ESP_LOGW(TAG, "emergency off");
}

}  // namespace esphome::inkplate_spi
