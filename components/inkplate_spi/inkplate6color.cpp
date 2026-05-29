#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "inkplate6color.h"

namespace esphome::inkplate_spi {

static const char *TAG = "inkplate6color";

// Runtime register addresses
static constexpr uint8_t REG_DTM   = 0x10;
static constexpr uint8_t REG_DRF   = 0x12;
static constexpr uint8_t REG_PON   = 0x04;
static constexpr uint8_t REG_POF   = 0x02;
static constexpr uint8_t REG_SLEEP = 0x07;

// ---------------------------------------------------------------------------

void Inkplate6Color::dump_config() {
  ESP_LOGCONFIG(TAG, "Inkplate 6 Color %dx%d", this->width_, this->height_);
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
  this->pin_cs_->digital_write(false);
  this->pin_dc_->digital_write(false);
  this->enable();
  this->write_byte(cmd);
  this->disable();
  this->pin_cs_->digital_write(true);

  if (len > 0 && data != nullptr) {
    this->pin_cs_->digital_write(false);
    this->pin_dc_->digital_write(true);
    this->enable();
    this->write_array(data, len);
    this->disable();
    this->pin_cs_->digital_write(true);
  }
}

// ---------------------------------------------------------------------------
// Prepare
// ---------------------------------------------------------------------------

void Inkplate6Color::prepare_for_update_() {
  this->pon_sub_            = PON_SETUP;
  this->trf_sub_            = TRF_START_DTM;
  this->poff_sub_           = POFF_SEND;
  this->dsleep_sub_         = DSLEEP_PRE_DELAY;
  this->sub_start_ms_       = 0;
  this->trf_row_            = 0;
  this->pon_settle_started_ = false;
}

// ---------------------------------------------------------------------------
// Power-on sub-state machine
// ---------------------------------------------------------------------------

bool Inkplate6Color::do_power_on_step_() {
  uint32_t now = App.get_loop_component_start_time();
  switch (this->pon_sub_) {

    case PON_SETUP:
      this->pin_rst_->pin_mode(gpio::FLAG_OUTPUT);
      this->pin_dc_->pin_mode(gpio::FLAG_OUTPUT);
      this->pin_cs_->pin_mode(gpio::FLAG_OUTPUT);
      this->pin_busy_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
      this->pin_dc_->digital_write(true);    // idle high
      this->pin_cs_->digital_write(true);    // deselected
      this->pin_rst_->digital_write(false);  // hold RST low during pre-wait
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_PRE_WAIT;
      return false;

    case PON_PRE_WAIT:
      // RST held low for 100ms before the reset pulse — discharges panel
      // capacitors. From Arduino reference; skipping causes init failures.
      if (now - this->sub_start_ms_ < 100) return false;
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_RST_LOW_WAIT;
      return false;

    case PON_RST_LOW_WAIT:
      if (now - this->sub_start_ms_ < 10) return false;
      this->pin_rst_->digital_write(true);
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_RST_HIGH_WAIT;
      return false;

    case PON_RST_HIGH_WAIT:
      if (now - this->sub_start_ms_ < 200) return false;
      this->pon_sub_ = PON_WAIT_BUSY;
      return false;

    case PON_WAIT_BUSY:
      if (!this->is_busy_()) return false;  // wait BUSY HIGH — panel ready after reset
      this->pon_sub_ = PON_DONE;
      return true;

    case PON_DONE:
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// PON — 100ms settling required after init sequence, then second CDI write.
// Non-blocking: timer tracked via pon_settle_started_ + sub_start_ms_.
// ---------------------------------------------------------------------------

bool Inkplate6Color::do_send_pon_() {
  uint32_t now = App.get_loop_component_start_time();
  if (!this->pon_settle_started_) {
    this->sub_start_ms_ = now;
    this->pon_settle_started_ = true;
    return false;
  }
  if (now - this->sub_start_ms_ < 100) return false;
  this->pon_settle_started_ = false;
  const uint8_t v = 0x37;
  this->send_command_to_chip_(0x50, &v, 1, 1);
  return true;
}

// ---------------------------------------------------------------------------
// Transfer sub-state machine
// Sends resolution, streams pixel data, then sends PON and waits BUSY HIGH.
// ---------------------------------------------------------------------------

bool Inkplate6Color::do_transfer_step_() {
  // 8 rows × 300 bytes = 2400 bytes per tick (~9.6ms at 2MHz, ~0.19ms at 10MHz)
  const size_t rows          = (size_t) this->height_;
  const size_t bytes_per_row = (size_t) this->width_ / 2;

  switch (this->trf_sub_) {

    case TRF_START_DTM: {
      // 0x61 (resolution) already sent in init_sequence — no need to repeat here.
      // Send DTM command byte as its own CS transaction (DC low)
      this->pin_cs_->digital_write(false);
      this->pin_dc_->digital_write(false);
      this->enable();
      this->write_byte(REG_DTM);
      this->disable();
      this->pin_cs_->digital_write(true);

      // Open data transaction (DC high, CS low) — held open across ticks
      this->pin_cs_->digital_write(false);
      this->pin_dc_->digital_write(true);
      this->enable();
      this->trf_row_ = 0;
      this->trf_sub_ = TRF_STREAM_DATA;
      ESP_LOGD(TAG, "transfer: DTM start");
      return false;
    }

    case TRF_STREAM_DATA: {
      size_t end = std::min(this->trf_row_ + ROWS_PER_CHUNK, rows);
      for (size_t i = this->trf_row_; i < end; i++)
        this->write_array(this->buffer_ + i * bytes_per_row, bytes_per_row);
      this->trf_row_ = end;
      if (this->trf_row_ >= rows) {
        this->disable();
        this->pin_cs_->digital_write(true);
        this->trf_sub_ = TRF_SEND_PON;
        ESP_LOGD(TAG, "transfer: DTM done");
      }
      return false;
    }

    case TRF_SEND_PON:
      this->send_command_to_chip_(REG_PON, nullptr, 0, 1);
      this->trf_sub_ = TRF_WAIT_PON_BUSY;
      return false;

    case TRF_WAIT_PON_BUSY:
      if (!this->is_busy_()) return false;  // wait BUSY HIGH
      this->trf_sub_ = TRF_DONE;
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
  this->send_command_to_chip_(REG_DRF, nullptr, 0, 1);
}

// ---------------------------------------------------------------------------
// Power-off sub-state machine
// After POF, BUSY goes LOW when power-off completes — opposite of Inkplate13.
// ---------------------------------------------------------------------------

bool Inkplate6Color::do_power_off_step_() {
  uint32_t now = App.get_loop_component_start_time();
  switch (this->poff_sub_) {

    case POFF_SEND:
      this->send_command_to_chip_(REG_POF, nullptr, 0, 1);
      this->poff_sub_ = POFF_WAIT;
      return false;

    case POFF_WAIT:
      if (this->is_busy_()) return false;  // BUSY still HIGH — wait for it to go LOW
      this->sub_start_ms_ = now;
      this->poff_sub_ = POFF_DELAY;
      return false;

    case POFF_DELAY:
      if (now - this->sub_start_ms_ < 200) return false;
      this->poff_sub_ = POFF_DONE;
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
  return this->pin_busy_->digital_read();
}

// ---------------------------------------------------------------------------
// Deep sleep / emergency off
// ---------------------------------------------------------------------------

bool Inkplate6Color::do_deep_sleep_() {
  // 10ms before + 100ms after sleep command — datasheet requirement.
  // RST held low keeps the controller in hardware deep sleep between cycles.
  uint32_t now = App.get_loop_component_start_time();
  switch (this->dsleep_sub_) {
    case DSLEEP_PRE_DELAY:
      this->sub_start_ms_ = now;
      this->dsleep_sub_ = DSLEEP_SEND;
      return false;
    case DSLEEP_SEND:
      if (now - this->sub_start_ms_ < 10) return false;
      { const uint8_t v = 0xA5; this->send_command_to_chip_(REG_SLEEP, &v, 1, 1); }
      this->sub_start_ms_ = now;
      this->dsleep_sub_ = DSLEEP_POST_DELAY;
      return false;
    case DSLEEP_POST_DELAY:
      if (now - this->sub_start_ms_ < 100) return false;
      this->pin_dc_->pin_mode(gpio::FLAG_INPUT);
      this->pin_cs_->pin_mode(gpio::FLAG_INPUT);
      this->pin_busy_->pin_mode(gpio::FLAG_INPUT);
      this->pin_rst_->pin_mode(gpio::FLAG_OUTPUT);
      this->pin_rst_->digital_write(false);
      ESP_LOGD(TAG, "panel deep sleep");
      this->dsleep_sub_ = DSLEEP_PRE_DELAY;
      return true;
  }
  return false;
}

void Inkplate6Color::do_emergency_off_() {
  // Called by on_safe_shutdown() if mid-refresh (e.g., OTA during update).
  // Best-effort: drive RST low immediately without waiting for busy.
  this->pin_rst_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_rst_->digital_write(false);
  this->pin_cs_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_cs_->digital_write(true);
  ESP_LOGW(TAG, "emergency off");
}

}  // namespace esphome::inkplate_spi
