#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "inkplate2.h"

namespace esphome::inkplate_spi {

static const char *TAG = "inkplate2";

// Runtime register addresses
static constexpr uint8_t REG_DTM1  = 0x10;  // data transfer — BW plane
static constexpr uint8_t REG_DTM2  = 0x13;  // data transfer — RED plane
static constexpr uint8_t REG_DSTOP = 0x11;  // data stop
static constexpr uint8_t REG_DRF   = 0x12;  // display refresh
static constexpr uint8_t REG_POF   = 0x02;  // power off
static constexpr uint8_t REG_SLEEP = 0x07;  // deep sleep

// ---------------------------------------------------------------------------

void Inkplate2::dump_config() {
  ESP_LOGCONFIG(TAG, "Inkplate 2 %dx%d", this->width_, this->height_);
}

// ---------------------------------------------------------------------------
// Color mapping — 3-color panel: black / white / red
// WHITE=1 matches base class buffer init (0x11 = both nibbles 1 = white).
// ---------------------------------------------------------------------------

uint8_t Inkplate2::map_color_to_index_(Color color) {
  uint8_t r = color.red, g = color.green, b = color.blue;
  if (r > 200 && g > 200 && b > 200) return 1;  // WHITE
  if (r > 150 && g < 100 && b < 100) return 2;  // RED
  return 0;                                       // BLACK (default)
}

// ---------------------------------------------------------------------------
// SPI — DC low for cmd byte (own CS transaction), DC high for data bytes.
// Identical to Inkplate6Color; same controller communication pattern.
// ---------------------------------------------------------------------------

void Inkplate2::send_command_to_chip_(uint8_t cmd, const uint8_t *data, size_t len, uint8_t /*chip*/) {
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

void Inkplate2::prepare_for_update_() {
  this->pon_sub_      = PON_SETUP;
  this->trf_sub_      = TRF_SEND_BW;
  this->poff_sub_     = POFF_SEND_VCOM;
  this->sub_start_ms_ = 0;
  this->trf_row_      = 0;
}

// ---------------------------------------------------------------------------
// Power-on sub-state machine
// Arduino order: resetPanel() → sendCommand(0x04) → waitForEpd() → settings → delay(20)
// PON (0x04) must precede panel settings — handled entirely here so INIT step
// (which fires after POWER_ON) uses an empty init_sequence and is a no-op.
// ---------------------------------------------------------------------------

bool Inkplate2::do_power_on_step_() {
  uint32_t now = App.get_loop_component_start_time();
  switch (this->pon_sub_) {

    case PON_SETUP:
      this->pin_rst_->pin_mode(gpio::FLAG_OUTPUT);
      this->pin_dc_->pin_mode(gpio::FLAG_OUTPUT);
      this->pin_cs_->pin_mode(gpio::FLAG_OUTPUT);
      this->pin_busy_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
      this->pin_dc_->digital_write(true);    // idle high
      this->pin_cs_->digital_write(true);    // deselected
      this->pin_rst_->digital_write(false);  // RST low — start reset pulse
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_RST_LOW_WAIT;
      return false;

    case PON_RST_LOW_WAIT:
      if (now - this->sub_start_ms_ < 100) return false;
      this->pin_rst_->digital_write(true);   // RST high — end reset pulse
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_RST_HIGH_WAIT;
      return false;

    case PON_RST_HIGH_WAIT:
      if (now - this->sub_start_ms_ < 100) return false;
      this->send_command_to_chip_(0x04, nullptr, 0, 1);  // PON — power on circuitry
      this->pon_sub_ = PON_WAIT_PON_BUSY;
      return false;

    case PON_WAIT_PON_BUSY:
      if (!this->is_busy_()) return false;  // wait BUSY HIGH — panel powered on
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_PON_BUSY_DELAY;
      return false;

    case PON_PON_BUSY_DELAY:
      if (now - this->sub_start_ms_ < 200) return false;  // 200ms from Arduino waitForEpd
      this->pon_sub_ = PON_SEND_SETTINGS;
      return false;

    case PON_SEND_SETTINGS: {
      const uint8_t ps[]   = {0x0F, 0x89};  // panel setting: LUT from OTP, temp sensor
      const uint8_t res[]  = {(uint8_t) this->width_,
                               (uint8_t)(this->height_ >> 8),
                               (uint8_t)(this->height_ & 0xFF)};
      const uint8_t vcom[] = {0x77};  // VCOM and data interval
      this->send_command_to_chip_(0x00, ps,   2, 1);
      this->send_command_to_chip_(0x61, res,  3, 1);
      this->send_command_to_chip_(0x50, vcom, 1, 1);
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_BOOT_DELAY;
      return false;
    }

    case PON_BOOT_DELAY:
      if (now - this->sub_start_ms_ < 20) return false;  // 20ms from Arduino display() delay
      this->pon_sub_ = PON_DONE;
      return true;

    case PON_DONE:
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Transfer sub-state machine
// Sends two 1bpp planes derived on-the-fly from the 4bpp nibble-packed buffer.
//   BW plane (DTM1 0x10): bit=0 → BLACK, bit=1 → WHITE or RED
//   RED plane (DTM2 0x13): bit=0 → RED,   bit=1 → WHITE or BLACK
// MSB-first: pixel x=0 → bit 7 of first byte of each row.
// CS held open across ticks during streaming; 8 rows/tick (~104 bytes at 1MHz ≈ 0.8ms).
// ---------------------------------------------------------------------------

bool Inkplate2::do_transfer_step_() {
  // 8 rows × 13 bytes = 104 bytes per tick (~0.8ms at 1MHz)
  const size_t rows      = (size_t) this->height_;
  const size_t buf_bpr   = (size_t) this->width_ / 2;  // 4bpp: 52 bytes/row
  const size_t plane_bpr = (size_t) this->width_ / 8;  // 1bpp: 13 bytes/row

  switch (this->trf_sub_) {

    case TRF_SEND_BW:
      // DTM1 command byte sent as its own CS transaction (DC low).
      // Data transaction (DC high, CS low) then held open across ticks.
      this->pin_cs_->digital_write(false);
      this->pin_dc_->digital_write(false);
      this->enable();
      this->write_byte(REG_DTM1);
      this->disable();
      this->pin_cs_->digital_write(true);
      this->pin_cs_->digital_write(false);
      this->pin_dc_->digital_write(true);
      this->enable();
      this->trf_row_ = 0;
      this->trf_sub_ = TRF_STREAM_BW;
      ESP_LOGD(TAG, "transfer: DTM1 BW start");
      return false;

    case TRF_STREAM_BW: {
      size_t end = std::min(this->trf_row_ + ROWS_PER_CHUNK, rows);
      for (size_t r = this->trf_row_; r < end; r++) {
        const uint8_t *row = this->buffer_ + r * buf_bpr;
        for (size_t b = 0; b < plane_bpr; b++) {
          uint8_t byte = 0xFF;
          for (int bit = 0; bit < 8; bit++) {
            int     px  = (int)(b * 8 + bit);
            uint8_t idx = (px % 2 == 0) ? ((row[px / 2] >> 4) & 0x0F)
                                         :  (row[px / 2]       & 0x0F);
            if (idx == 0) byte &= ~(1u << (7 - bit));  // BLACK → 0 bit
          }
          this->write_byte(byte);
        }
      }
      this->trf_row_ = end;
      if (this->trf_row_ >= rows) {
        this->disable();
        this->pin_cs_->digital_write(true);
        this->trf_row_ = 0;
        this->trf_sub_ = TRF_SEND_RED;
        ESP_LOGD(TAG, "transfer: DTM1 BW done");
      }
      return false;
    }

    case TRF_SEND_RED:
      // Same CS pattern as TRF_SEND_BW: command in own transaction, data held open.
      this->pin_cs_->digital_write(false);
      this->pin_dc_->digital_write(false);
      this->enable();
      this->write_byte(REG_DTM2);
      this->disable();
      this->pin_cs_->digital_write(true);
      this->pin_cs_->digital_write(false);
      this->pin_dc_->digital_write(true);
      this->enable();
      this->trf_sub_ = TRF_STREAM_RED;
      ESP_LOGD(TAG, "transfer: DTM2 RED start");
      return false;

    case TRF_STREAM_RED: {
      size_t end = std::min(this->trf_row_ + ROWS_PER_CHUNK, rows);
      for (size_t r = this->trf_row_; r < end; r++) {
        const uint8_t *row = this->buffer_ + r * buf_bpr;
        for (size_t b = 0; b < plane_bpr; b++) {
          uint8_t byte = 0xFF;
          for (int bit = 0; bit < 8; bit++) {
            int     px  = (int)(b * 8 + bit);
            uint8_t idx = (px % 2 == 0) ? ((row[px / 2] >> 4) & 0x0F)
                                         :  (row[px / 2]       & 0x0F);
            if (idx == 2) byte &= ~(1u << (7 - bit));  // RED → 0 bit
          }
          this->write_byte(byte);
        }
      }
      this->trf_row_ = end;
      if (this->trf_row_ >= rows) {
        this->disable();
        this->pin_cs_->digital_write(true);
        this->trf_sub_ = TRF_SEND_STOP;
        ESP_LOGD(TAG, "transfer: DTM2 RED done");
      }
      return false;
    }

    case TRF_SEND_STOP: {
      const uint8_t v = 0x00;
      this->send_command_to_chip_(REG_DSTOP, &v, 1, 1);
      this->trf_sub_ = TRF_DONE;
      return true;
    }

    case TRF_DONE:
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void Inkplate2::do_send_refresh_() {
  this->send_command_to_chip_(REG_DRF, nullptr, 0, 1);
}

// ---------------------------------------------------------------------------
// Power-off sub-state machine
// Arduino: sendCommand(0x50)+sendData(0xF7) → POF(0x02) → waitForEpd() → done
// BUSY goes HIGH when power-off completes (same polarity as other wait states).
// ---------------------------------------------------------------------------

bool Inkplate2::do_power_off_step_() {
  uint32_t now = App.get_loop_component_start_time();
  switch (this->poff_sub_) {

    case POFF_SEND_VCOM: {
      const uint8_t v = 0xF7;
      this->send_command_to_chip_(0x50, &v, 1, 1);
      this->poff_sub_ = POFF_SEND_POF;
      return false;
    }

    case POFF_SEND_POF:
      this->send_command_to_chip_(REG_POF, nullptr, 0, 1);
      this->poff_sub_ = POFF_WAIT;
      return false;

    case POFF_WAIT:
      if (!this->is_busy_()) return false;  // wait BUSY HIGH — power-off complete
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
// Busy pin — HIGH = ready / not busy
// ---------------------------------------------------------------------------

bool Inkplate2::is_busy_() {
  return this->pin_busy_->digital_read();
}

// ---------------------------------------------------------------------------
// Deep sleep / emergency off
// ---------------------------------------------------------------------------

bool Inkplate2::do_deep_sleep_() {
  // Controller enters software deep sleep via 0x07/0xA5 command.
  // All pins released to INPUT after — RST not held low since the sleep
  // command itself parks the controller (unlike Inkplate13 which needs RST low).
  const uint8_t v = 0xA5;
  this->send_command_to_chip_(REG_SLEEP, &v, 1, 1);
  this->pin_dc_->pin_mode(gpio::FLAG_INPUT);
  this->pin_cs_->pin_mode(gpio::FLAG_INPUT);
  this->pin_busy_->pin_mode(gpio::FLAG_INPUT);
  this->pin_rst_->pin_mode(gpio::FLAG_INPUT);
  ESP_LOGD(TAG, "panel deep sleep");
  return true;
}

void Inkplate2::do_emergency_off_() {
  // Called by on_safe_shutdown() if mid-refresh (e.g., OTA during update).
  // Best-effort: drive RST low immediately without waiting for busy.
  this->pin_rst_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_rst_->digital_write(false);
  this->pin_cs_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_cs_->digital_write(true);
  ESP_LOGW(TAG, "emergency off");
}

}  // namespace esphome::inkplate_spi
