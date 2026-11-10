#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "inkplate7spectra.h"

namespace esphome::inkplate_spi {

static const char *TAG = "inkplate7spectra";

// Runtime register addresses
static constexpr uint8_t REG_PSR   = 0x00;
static constexpr uint8_t REG_POF   = 0x02;
static constexpr uint8_t REG_PON   = 0x04;
static constexpr uint8_t REG_BTST2 = 0x06;
static constexpr uint8_t REG_DTM   = 0x10;
static constexpr uint8_t REG_DRF   = 0x12;

static constexpr uint8_t POF_V[]      = {0x00};
static constexpr uint8_t DRF_V[]      = {0x00};
// BTST2 second setting — sent after PON, right before DRF.
static constexpr uint8_t BTST2_V2[]   = {0x6F, 0x1F, 0x17, 0x49};
// PSR value the panel must get before its supply is cut.
static constexpr uint8_t PSR_OFF_V[]  = {0x4F, 0x6E};

// BUSYN is only valid this long after a command.
static constexpr uint32_t BUSY_SETTLE_MS = 2;

// ---------------------------------------------------------------------------

void Inkplate7Spectra::dump_config() {
  ESP_LOGCONFIG(TAG, "Inkplate 7 Spectra %dx%d", this->width_, this->height_);
  LOG_PIN("  RST: ", this->pin_rst_);
  LOG_PIN("  DC: ", this->pin_dc_);
  LOG_PIN("  CS: ", this->pin_cs_);
  LOG_PIN("  BUSY: ", this->pin_busy_);
  LOG_PIN("  PWR_EN: ", this->pin_pwr_en_);
  LOG_PIN("  BS0: ", this->pin_bs0_);
  LOG_PIN("  BS1: ", this->pin_bs1_);
}

// ---------------------------------------------------------------------------
// InkplateBase interface
// ---------------------------------------------------------------------------

uint8_t Inkplate7Spectra::map_color_to_index_(Color color) {
  // Spectra 6 native indices: BLACK=0 WHITE=1 YELLOW=2 RED=3 BLUE=5 GREEN=6.
  uint8_t r = color.red, g = color.green, b = color.blue;
  if (r > 200 && g > 200 && b > 200) return 0x01;  // WHITE
  if (r < 50  && g < 50  && b < 50 ) return 0x00;  // BLACK
  if (r > 150 && g < 100 && b < 100) return 0x03;  // RED
  if (r < 100 && g > 150 && b < 100) return 0x06;  // GREEN
  if (r < 100 && g < 100 && b > 150) return 0x05;  // BLUE
  if (r > 150 && g > 150 && b < 100) return 0x02;  // YELLOW
  return 0x00;
}

void Inkplate7Spectra::send_command_to_chip_(uint8_t cmd, const uint8_t *data, size_t len, uint8_t /*chip*/) {
  this->pin_dc_->digital_write(false);
  this->pin_cs_->digital_write(false);
  this->enable();
  this->write_byte(cmd);
  this->disable();
  this->pin_cs_->digital_write(true);
  this->pin_dc_->digital_write(true);

  if (len > 0 && data != nullptr) {
    this->pin_cs_->digital_write(false);
    this->enable();
    this->write_array(data, len);
    this->disable();
    this->pin_cs_->digital_write(true);
  }
  this->last_cmd_ms_ = millis();
}

void Inkplate7Spectra::prepare_for_update_() {
  this->pon_sub_      = PON_PINS_LOW;
  this->trf_sub_      = TRF_STREAM;
  this->poff_sub_     = POFF_SEND;
  this->sub_start_ms_ = 0;
  this->trf_row_      = 0;
}

bool Inkplate7Spectra::is_busy_() {
  // Returns true when the panel is READY (BUSYN high), per InkplateBase convention.
  if (millis() - this->last_cmd_ms_ < BUSY_SETTLE_MS) return false;
  return this->pin_busy_->digital_read();
}

// ---------------------------------------------------------------------------
// Power-on: pins low → PWR_EN → dual reset → busy → 10 ms
// ---------------------------------------------------------------------------

bool Inkplate7Spectra::do_power_on_step_() {
  uint32_t now = millis();
  switch (this->pon_sub_) {
    case PON_PINS_LOW:
      this->set_all_pins_low_();
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_PINS_LOW_WAIT;
      return false;

    case PON_PINS_LOW_WAIT:
      if (now - this->sub_start_ms_ < 50) return false;
      this->set_io_pins_();
      this->pin_pwr_en_->digital_write(true);
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_IO_WAIT;
      return false;

    case PON_IO_WAIT:
      if (now - this->sub_start_ms_ < 100) return false;
      this->pin_rst_->digital_write(false);
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_RST_1_LOW;
      return false;

    case PON_RST_1_LOW:
      if (now - this->sub_start_ms_ < 35) return false;
      this->pin_rst_->digital_write(true);
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_RST_1_HIGH;
      return false;

    case PON_RST_1_HIGH:
      if (now - this->sub_start_ms_ < 35) return false;
      this->pin_rst_->digital_write(false);
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_RST_2_LOW;
      return false;

    case PON_RST_2_LOW:
      if (now - this->sub_start_ms_ < 35) return false;
      this->pin_rst_->digital_write(true);
      this->last_cmd_ms_ = now;  // reuse busy settle delay after reset release
      this->pon_sub_ = PON_WAIT_BUSY;
      return false;

    case PON_WAIT_BUSY:
      if (!this->is_busy_()) return false;
      this->sub_start_ms_ = now;
      this->pon_sub_ = PON_SETTLE;
      return false;

    case PON_SETTLE:
      if (now - this->sub_start_ms_ < 10) return false;
      this->pon_sub_ = PON_DONE;
      return true;

    case PON_DONE:
      return true;
  }
  return false;
}

// PON is sent after the pixel data (see do_transfer_step_()).
bool Inkplate7Spectra::do_send_pon_() { return true; }

// ---------------------------------------------------------------------------
// Transfer: DTM + data → PON → busy → BTST2 second setting
// ---------------------------------------------------------------------------

bool Inkplate7Spectra::do_transfer_step_() {
  const size_t rows          = (size_t) this->height_;
  const size_t bytes_per_row = (size_t) this->width_ / 2;

  switch (this->trf_sub_) {
    case TRF_STREAM:
      if (this->trf_row_ == 0) {
        this->pin_dc_->digital_write(false);
        this->pin_cs_->digital_write(false);
        this->enable();
        this->write_byte(REG_DTM);
        this->disable();
        this->pin_cs_->digital_write(true);
        this->pin_dc_->digital_write(true);
        // CS stays low across loop() ticks until all rows are sent.
        this->pin_cs_->digital_write(false);
        this->enable();
      }
      {
        size_t end = std::min(this->trf_row_ + ROWS_PER_CHUNK, rows);
        for (size_t i = this->trf_row_; i < end; i++)
          this->write_array(this->buffer_ + i * bytes_per_row, bytes_per_row);
        this->trf_row_ = end;
      }
      if (this->trf_row_ >= rows) {
        this->disable();
        this->pin_cs_->digital_write(true);
        this->send_command_to_chip_(REG_PON, nullptr, 0, 1);
        this->trf_sub_ = TRF_WAIT_PON;
      }
      return false;

    case TRF_WAIT_PON:
      if (!this->is_busy_()) return false;
      this->send_command_to_chip_(REG_BTST2, BTST2_V2, sizeof(BTST2_V2), 1);
      this->trf_sub_ = TRF_DONE;
      return true;

    case TRF_DONE:
      return true;
  }
  return false;
}

void Inkplate7Spectra::do_send_refresh_() {
  this->send_command_to_chip_(REG_DRF, DRF_V, sizeof(DRF_V), 1);
}

// ---------------------------------------------------------------------------
// Power-off: POF → busy → 15 ms → PSR VDD-off → busy → pins low → PWR_EN low
// ---------------------------------------------------------------------------

bool Inkplate7Spectra::do_power_off_step_() {
  uint32_t now = millis();
  switch (this->poff_sub_) {
    case POFF_SEND:
      this->send_command_to_chip_(REG_POF, POF_V, sizeof(POF_V), 1);
      this->poff_sub_ = POFF_WAIT;
      return false;

    case POFF_WAIT:
      if (!this->is_busy_()) return false;
      this->sub_start_ms_ = now;
      this->poff_sub_ = POFF_DELAY;
      return false;

    case POFF_DELAY:
      if (now - this->sub_start_ms_ < 15) return false;
      this->send_command_to_chip_(REG_PSR, PSR_OFF_V, sizeof(PSR_OFF_V), 1);
      this->poff_sub_ = POFF_PSR_WAIT;
      return false;

    case POFF_PSR_WAIT:
      if (!this->is_busy_()) return false;
      // shortcut: MOSI is owned by the ESPHome SPI bus and is not forced low like the
      // Arduino driver does; drive it as GPIO here if hardware tests show it matters.
      this->pin_cs_->digital_write(false);
      this->pin_rst_->digital_write(false);
      this->pin_pwr_en_->digital_write(false);
      this->pin_dc_->pin_mode(gpio::FLAG_INPUT);
      this->pin_cs_->pin_mode(gpio::FLAG_INPUT);
      this->pin_rst_->pin_mode(gpio::FLAG_INPUT);
      this->pin_busy_->pin_mode(gpio::FLAG_INPUT);
      this->pin_pwr_en_->pin_mode(gpio::FLAG_INPUT);
      ESP_LOGD(TAG, "panel powered off");
      this->poff_sub_ = POFF_DONE;
      return true;

    case POFF_DONE:
      return true;
  }
  return false;
}

void Inkplate7Spectra::do_emergency_off_() {
  this->pin_pwr_en_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_pwr_en_->digital_write(false);
  this->pin_rst_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_rst_->digital_write(false);
  ESP_LOGW(TAG, "emergency off");
}

// ---------------------------------------------------------------------------
// GPIO helpers
// ---------------------------------------------------------------------------

void Inkplate7Spectra::set_all_pins_low_() {
  // Drains the panel supply capacitors before power-up; the Arduino driver notes the
  // panel sometimes refuses to refresh without this.
  GPIOPin *pins[] = {
    this->pin_rst_, this->pin_dc_, this->pin_cs_, this->pin_busy_,
    this->pin_pwr_en_, this->pin_bs0_, this->pin_bs1_,
  };
  for (auto *p : pins) {
    p->pin_mode(gpio::FLAG_OUTPUT);
    p->digital_write(false);
  }
}

void Inkplate7Spectra::set_io_pins_() {
  this->pin_rst_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_dc_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_cs_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_busy_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
  this->pin_pwr_en_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_bs0_->pin_mode(gpio::FLAG_OUTPUT);
  this->pin_bs1_->pin_mode(gpio::FLAG_OUTPUT);

  // BS0 and BS1 low select the 4-wire SPI interface mode.
  this->pin_dc_->digital_write(true);
  this->pin_cs_->digital_write(true);
  this->pin_rst_->digital_write(false);
  this->pin_pwr_en_->digital_write(false);
  this->pin_bs0_->digital_write(false);
  this->pin_bs1_->digital_write(false);
}

}  // namespace esphome::inkplate_spi
