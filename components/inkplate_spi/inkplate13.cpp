#include "esphome/core/log.h"
#include "inkplate13.h"

#include "driver/gpio.h"

namespace esphome::inkplate_spi {

static const char *TAG = "inkplate13";

// Runtime register addresses
static constexpr uint8_t REG_DTM  = 0x10;
static constexpr uint8_t REG_DRF  = 0x12;
static constexpr uint8_t REG_PON  = 0x04;
static constexpr uint8_t REG_POF  = 0x02;
static constexpr uint8_t REG_PTLW = 0x83;  // partial window
static constexpr uint8_t REG_CMD66= 0xF0;  // waveform select (required before PTLW)

static constexpr uint8_t DRF_V[]   = {0x00};
static constexpr uint8_t POF_V[]   = {0x00};
static constexpr uint8_t CMD66_V[] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};

// Chip-target bitmask (must match models/inkplate13.py)
static constexpr uint8_t CHIP_MASTER = 1;
static constexpr uint8_t CHIP_SLAVE  = 2;
static constexpr uint8_t CHIP_BOTH   = 3;

// Convenience cast helper
#define PIN(x) static_cast<gpio_num_t>(x)

// ---------------------------------------------------------------------------

void Inkplate13::dump_config() {
  ESP_LOGCONFIG(TAG, "Inkplate 13 Spectra %dx%d", width_, height_);
  ESP_LOGCONFIG(TAG, "  RST=%d DC=%d BUSY=%d PWR_EN=%d", pin_rst_, pin_dc_, pin_busy_, pin_pwr_en_);
  ESP_LOGCONFIG(TAG, "  CS_M=%d CS_S=%d BS0=%d BS1=%d", pin_cs_m_, pin_cs_s_, pin_bs0_, pin_bs1_);
}

// ---------------------------------------------------------------------------
// InkplateSPIBase interface
// ---------------------------------------------------------------------------

uint8_t Inkplate13::map_color_to_index_(Color color) {
  uint8_t r = color.red, g = color.green, b = color.blue;
  if (r > 200 && g > 200 && b > 200) return 0x01;  // WHITE
  if (r < 50  && g < 50  && b < 50 ) return 0x00;  // BLACK
  if (r > 150 && g < 100 && b < 100) return 0x03;  // RED
  if (r < 100 && g > 150 && b < 100) return 0x06;  // GREEN
  if (r < 100 && g < 100 && b > 150) return 0x05;  // BLUE
  if (r > 150 && g > 150 && b < 100) return 0x02;  // YELLOW
  return 0x00;
}

void Inkplate13::send_command_to_chip_(uint8_t cmd, const uint8_t *data, size_t len, uint8_t chip) {
  if (chip & CHIP_MASTER) gpio_set_level(PIN(pin_cs_m_), 0);
  if (chip & CHIP_SLAVE)  gpio_set_level(PIN(pin_cs_s_), 0);

  this->enable();
  this->write_byte(cmd);
  if (len > 0 && data != nullptr)
    this->write_array(data, len);
  this->disable();

  if (chip & CHIP_MASTER) gpio_set_level(PIN(pin_cs_m_), 1);
  if (chip & CHIP_SLAVE)  gpio_set_level(PIN(pin_cs_s_), 1);
}

void Inkplate13::prepare_for_update_() {
  pon_sub_      = PON_PINS_LOW;
  trf_sub_      = partial_ ? TRF_PARTIAL_SETUP_M : TRF_MASTER;
  poff_sub_     = POFF_SEND;
  sub_start_ms_ = 0;
  trf_row_      = 0;
  if (partial_) compute_ptlw_params_();
}

// ---------------------------------------------------------------------------
// Partial-window parameter computation
// ---------------------------------------------------------------------------

void Inkplate13::compute_ptlw_params_() {
  ptlw_master_.needed = false;
  ptlw_slave_.needed  = false;

  int x = partial_x_, y = partial_y_, w = partial_w_, h = partial_h_;

  // Clip to logical (rotation-aware) canvas
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > this->get_width())  w = this->get_width()  - x;
  if (y + h > this->get_height()) h = this->get_height() - y;
  if (w <= 0 || h <= 0) return;

  const int16_t W = (int16_t) width_;   // physical panel width  = 1200
  const int16_t H = (int16_t) height_;  // physical panel height = 1600

  // Map logical (rotated) coords → physical col/row
  int16_t colStart, colEnd, rowStart, rowEnd;
  switch ((int) this->rotation_) {
    case 90:
      colStart = W - y - h; colEnd = W - 1 - y;
      rowStart = x;         rowEnd = x + w - 1;
      break;
    case 180:
      colStart = x;         colEnd = x + w - 1;
      rowStart = y;         rowEnd = y + h - 1;
      break;
    case 270:
      colStart = y;          colEnd = y + h - 1;
      rowStart = H - x - w;  rowEnd = H - 1 - x;
      break;
    default:  // 0
      colStart = W - x - w;  colEnd = W - 1 - x;
      rowStart = H - y - h;  rowEnd = H - 1 - y;
      break;
  }

  // PTLW hardware alignment constraints: cols → multiples of 4, rows → even
  colStart = (colStart / 4) * 4;
  colEnd   = (((colEnd + 4) / 4) * 4) - 1;
  if (colEnd   >= W) colEnd   = W - 1;
  if (rowStart  % 2) rowStart--;
  if (rowStart  < 0) rowStart = 0;
  if ((rowEnd + 1) % 2) rowEnd++;
  if (rowEnd   >= H) rowEnd   = H - 1;

  const int16_t HALF_W     = W / 2;      // 600 physical cols per chip
  const int16_t HALF_BYTES = HALF_W / 2; // 300 bytes per row per chip

  // Master chip handles physical cols 0 .. HALF_W-1
  if (colStart < HALF_W) {
    int16_t lcs = colStart;
    int16_t lce = (colEnd < HALF_W) ? colEnd : (int16_t)(HALF_W - 1);
    uint16_t HRST = (uint16_t) lcs * 2;
    uint16_t HRED = (uint16_t)(lce + 1) * 2 - 1;
    uint16_t VRST = (uint16_t) rowStart / 2;
    uint16_t VRED = (uint16_t)(rowEnd + 1) / 2 - 1;
    auto &p = ptlw_master_;
    p.ptlw[0] = HRST >> 8; p.ptlw[1] = HRST & 0xFF;
    p.ptlw[2] = HRED >> 8; p.ptlw[3] = HRED & 0xFF;
    p.ptlw[4] = VRST >> 8; p.ptlw[5] = VRST & 0xFF;
    p.ptlw[6] = VRED >> 8; p.ptlw[7] = VRED & 0xFF;
    p.ptlw[8]        = 0x01;
    p.bytes_per_row  = (lce - lcs + 1) / 2;
    p.mem_col_off    = lcs / 2;
    p.row_start      = rowStart;
    p.row_end        = rowEnd;
    p.needed         = true;
    ESP_LOGD(TAG, "ptlw_master HRST=%d HRED=%d VRST=%d VRED=%d bpr=%d",
             HRST, HRED, VRST, VRED, p.bytes_per_row);
  }

  // Slave chip handles physical cols HALF_W .. W-1
  if (colEnd >= HALF_W) {
    int16_t lcs = (colStart >= HALF_W) ? (int16_t)(colStart - HALF_W) : 0;
    int16_t lce = colEnd - HALF_W;
    uint16_t HRST = (uint16_t) lcs * 2;
    uint16_t HRED = (uint16_t)(lce + 1) * 2 - 1;
    uint16_t VRST = (uint16_t) rowStart / 2;
    uint16_t VRED = (uint16_t)(rowEnd + 1) / 2 - 1;
    auto &p = ptlw_slave_;
    p.ptlw[0] = HRST >> 8; p.ptlw[1] = HRST & 0xFF;
    p.ptlw[2] = HRED >> 8; p.ptlw[3] = HRED & 0xFF;
    p.ptlw[4] = VRST >> 8; p.ptlw[5] = VRST & 0xFF;
    p.ptlw[6] = VRED >> 8; p.ptlw[7] = VRED & 0xFF;
    p.ptlw[8]        = 0x01;
    p.bytes_per_row  = (lce - lcs + 1) / 2;
    p.mem_col_off    = HALF_BYTES + lcs / 2;
    p.row_start      = rowStart;
    p.row_end        = rowEnd;
    p.needed         = true;
    ESP_LOGD(TAG, "ptlw_slave  HRST=%d HRED=%d VRST=%d VRED=%d bpr=%d",
             HRST, HRED, VRST, VRED, p.bytes_per_row);
  }
}

// ---------------------------------------------------------------------------
// Power-on sub-state machine
// ---------------------------------------------------------------------------

bool Inkplate13::do_power_on_step_() {
  uint32_t now = millis();
  switch (pon_sub_) {

    case PON_PINS_LOW:
      set_all_pins_low_();
      sub_start_ms_ = now;
      pon_sub_ = PON_PINS_LOW_WAIT;
      return false;

    case PON_PINS_LOW_WAIT:
      if (now - sub_start_ms_ < 50) return false;
      set_io_pins_();
      gpio_set_level(PIN(pin_pwr_en_), 1);
      sub_start_ms_ = now;
      pon_sub_ = PON_IO_WAIT;
      return false;

    case PON_IO_WAIT:
      if (now - sub_start_ms_ < 100) return false;
      gpio_set_level(PIN(pin_rst_), 0);
      sub_start_ms_ = now;
      pon_sub_ = PON_RESET_LOW_WAIT;
      return false;

    case PON_RESET_LOW_WAIT:
      if (now - sub_start_ms_ < 100) return false;
      gpio_set_level(PIN(pin_rst_), 1);
      sub_start_ms_ = now;
      pon_sub_ = PON_RESET_HIGH_WAIT;
      return false;

    case PON_RESET_HIGH_WAIT:
      if (now - sub_start_ms_ < 100) return false;
      pon_sub_ = PON_DONE;
      return true;

    case PON_DONE:
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// PON command
// ---------------------------------------------------------------------------

void Inkplate13::do_send_pon_() {
  send_command_to_chip_(REG_PON, nullptr, 0, CHIP_BOTH);
}

// ---------------------------------------------------------------------------
// Transfer sub-state machine (master → busy → slave → busy)
// ---------------------------------------------------------------------------

bool Inkplate13::do_transfer_step_() {
  // Number of rows sent per loop() tick — tunes blocking time per call.
  // 16 rows × 300 bytes = 4800 bytes ≈ 3.8 ms at 10 MHz.
  static constexpr size_t ROWS_PER_CHUNK = 16;

  const size_t rows          = (size_t) height_;
  const size_t bytes_per_row = (size_t) width_ / 2;
  const size_t half          = bytes_per_row / 2;  // bytes per row per chip

  switch (trf_sub_) {

    case TRF_MASTER:
      // Open CS + SPI on first chunk only
      if (trf_row_ == 0) {
        ESP_LOGD(TAG, "transfer: master start");
        gpio_set_level(PIN(pin_cs_m_), 0);
        this->enable();
        this->write_byte(REG_DTM);
      }
      {
        size_t end = std::min(trf_row_ + ROWS_PER_CHUNK, rows);
        for (size_t i = trf_row_; i < end; i++)
          this->write_array(buffer_ + i * bytes_per_row, half);
        trf_row_ = end;
      }
      if (trf_row_ >= rows) {
        this->disable();
        gpio_set_level(PIN(pin_cs_m_), 1);
        trf_row_  = 0;
        trf_sub_  = TRF_WAIT_MASTER;
        ESP_LOGD(TAG, "transfer: master done");
      }
      return false;

    case TRF_WAIT_MASTER:
      if (!is_busy_()) return false;
      trf_sub_ = TRF_SLAVE;
      return false;

    case TRF_SLAVE:
      if (trf_row_ == 0) {
        ESP_LOGD(TAG, "transfer: slave start");
        gpio_set_level(PIN(pin_cs_s_), 0);
        this->enable();
        this->write_byte(REG_DTM);
      }
      {
        size_t end = std::min(trf_row_ + ROWS_PER_CHUNK, rows);
        for (size_t i = trf_row_; i < end; i++)
          this->write_array(buffer_ + i * bytes_per_row + half, half);
        trf_row_ = end;
      }
      if (trf_row_ >= rows) {
        this->disable();
        gpio_set_level(PIN(pin_cs_s_), 1);
        trf_row_  = 0;
        trf_sub_  = TRF_WAIT_SLAVE;
        ESP_LOGD(TAG, "transfer: slave done");
      }
      return false;

    case TRF_WAIT_SLAVE:
      if (!is_busy_()) return false;
      trf_sub_ = TRF_DONE;
      return true;

    case TRF_DONE:
      return true;

    // --- Partial path ---------------------------------------------------

    // Null PTLW: 4-col × 4-row window at physical origin of chip.
    // Sent to chips not involved in the partial region so the controller
    // completes a full CMD66→PTLW→DTM→DRF cycle on both chips.
    // HRST=0 HRED=7 VRST=0 VRED=1 PT=1  →  cols 0-3, rows 0-3, bytesPerRow=2
    case TRF_PARTIAL_SETUP_M: {
      send_command_to_chip_(REG_CMD66, CMD66_V, sizeof(CMD66_V), CHIP_MASTER);
      if (!ptlw_master_.needed) {
        static constexpr uint8_t NULL_PTLW[9] = {
          0x00,0x00, 0x00,0x07, 0x00,0x00, 0x00,0x01, 0x01
        };
        send_command_to_chip_(REG_PTLW, NULL_PTLW, 9, CHIP_MASTER);
        gpio_set_level(PIN(pin_cs_m_), 0);
        this->enable();
        this->write_byte(REG_DTM);
        for (int r = 0; r < 4; r++)
          this->write_array(buffer_ + r * (size_t)(width_ / 2), 2);
        this->disable();
        gpio_set_level(PIN(pin_cs_m_), 1);
        trf_sub_ = TRF_PARTIAL_WAIT_M;
      } else {
        ESP_LOGD(TAG, "partial: master CMD66+PTLW+DTM start");
        send_command_to_chip_(REG_PTLW, ptlw_master_.ptlw, 9, CHIP_MASTER);
        gpio_set_level(PIN(pin_cs_m_), 0);
        this->enable();
        this->write_byte(REG_DTM);
        trf_row_ = ptlw_master_.row_start;
        trf_sub_ = TRF_PARTIAL_DATA_M;
      }
      return false;
    }

    case TRF_PARTIAL_DATA_M: {
      size_t end = std::min(trf_row_ + ROWS_PER_CHUNK,
                            (size_t)(ptlw_master_.row_end + 1));
      for (size_t i = trf_row_; i < end; i++)
        this->write_array(buffer_ + i * (size_t)(width_ / 2) + ptlw_master_.mem_col_off,
                          ptlw_master_.bytes_per_row);
      trf_row_ = end;
      if (trf_row_ > (size_t) ptlw_master_.row_end) {
        this->disable();
        gpio_set_level(PIN(pin_cs_m_), 1);
        trf_sub_ = TRF_PARTIAL_WAIT_M;
        ESP_LOGD(TAG, "partial: master DTM done");
      }
      return false;
    }

    case TRF_PARTIAL_WAIT_M:
      if (!is_busy_()) return false;
      trf_sub_ = TRF_PARTIAL_SETUP_S;
      return false;

    case TRF_PARTIAL_SETUP_S: {
      send_command_to_chip_(REG_CMD66, CMD66_V, sizeof(CMD66_V), CHIP_SLAVE);
      if (!ptlw_slave_.needed) {
        static constexpr uint8_t NULL_PTLW[9] = {
          0x00,0x00, 0x00,0x07, 0x00,0x00, 0x00,0x01, 0x01
        };
        send_command_to_chip_(REG_PTLW, NULL_PTLW, 9, CHIP_SLAVE);
        gpio_set_level(PIN(pin_cs_s_), 0);
        this->enable();
        this->write_byte(REG_DTM);
        for (int r = 0; r < 4; r++)
          this->write_array(buffer_ + r * (size_t)(width_ / 2) + (width_ / 4), 2);
        this->disable();
        gpio_set_level(PIN(pin_cs_s_), 1);
        trf_sub_ = TRF_PARTIAL_WAIT_S;
      } else {
        ESP_LOGD(TAG, "partial: slave CMD66+PTLW+DTM start");
        send_command_to_chip_(REG_PTLW, ptlw_slave_.ptlw, 9, CHIP_SLAVE);
        gpio_set_level(PIN(pin_cs_s_), 0);
        this->enable();
        this->write_byte(REG_DTM);
        trf_row_ = ptlw_slave_.row_start;
        trf_sub_ = TRF_PARTIAL_DATA_S;
      }
      return false;
    }

    case TRF_PARTIAL_DATA_S: {
      size_t end = std::min(trf_row_ + ROWS_PER_CHUNK,
                            (size_t)(ptlw_slave_.row_end + 1));
      for (size_t i = trf_row_; i < end; i++)
        this->write_array(buffer_ + i * (size_t)(width_ / 2) + ptlw_slave_.mem_col_off,
                          ptlw_slave_.bytes_per_row);
      trf_row_ = end;
      if (trf_row_ > (size_t) ptlw_slave_.row_end) {
        this->disable();
        gpio_set_level(PIN(pin_cs_s_), 1);
        trf_sub_ = TRF_PARTIAL_WAIT_S;
        ESP_LOGD(TAG, "partial: slave DTM done");
      }
      return false;
    }

    case TRF_PARTIAL_WAIT_S:
      if (!is_busy_()) return false;
      trf_sub_ = TRF_DONE;
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Refresh command
// ---------------------------------------------------------------------------

void Inkplate13::do_send_refresh_() {
  send_command_to_chip_(REG_DRF, DRF_V, sizeof(DRF_V), CHIP_BOTH);
}

// ---------------------------------------------------------------------------
// Power-off sub-state machine
// ---------------------------------------------------------------------------

bool Inkplate13::do_power_off_step_() {
  switch (poff_sub_) {

    case POFF_SEND:
      send_command_to_chip_(REG_POF, POF_V, sizeof(POF_V), CHIP_BOTH);
      poff_sub_ = POFF_WAIT;
      return false;

    case POFF_WAIT:
      if (!is_busy_()) return false;
      poff_sub_ = POFF_RELEASE;
      return false;

    case POFF_RELEASE:
      gpio_set_direction(PIN(pin_dc_),     GPIO_MODE_INPUT);
      gpio_set_direction(PIN(pin_cs_m_),   GPIO_MODE_INPUT);
      gpio_set_direction(PIN(pin_cs_s_),   GPIO_MODE_INPUT);
      // RST intentionally NOT released — do_deep_sleep_() will drive it low.
      gpio_set_direction(PIN(pin_busy_),   GPIO_MODE_INPUT);
      gpio_set_direction(PIN(pin_pwr_en_), GPIO_MODE_INPUT);
      gpio_set_level(PIN(pin_pwr_en_), 0);
      poff_sub_ = POFF_DONE;
      return true;

    case POFF_DONE:
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Deep sleep / emergency off
// ---------------------------------------------------------------------------

void Inkplate13::do_deep_sleep_() {
  // Hold RST low — keeps both controllers in deep sleep.
  // pwr_en is already 0 (released in POFF_RELEASE).
  // On next power-on cycle, set_all_pins_low_() + set_io_pins_() will
  // take over and drive RST through the normal reset sequence.
  gpio_set_direction(PIN(pin_rst_), GPIO_MODE_OUTPUT);
  gpio_set_level(PIN(pin_rst_), 0);
  ESP_LOGD(TAG, "panel deep sleep");
}

void Inkplate13::do_emergency_off_() {
  // Called by on_safe_shutdown() if mid-refresh (e.g., OTA during update).
  // Best-effort: kill power and RST immediately without waiting for busy.
  gpio_set_direction(PIN(pin_pwr_en_), GPIO_MODE_OUTPUT);
  gpio_set_level(PIN(pin_pwr_en_), 0);
  gpio_set_direction(PIN(pin_rst_), GPIO_MODE_OUTPUT);
  gpio_set_level(PIN(pin_rst_), 0);
  ESP_LOGW(TAG, "emergency off");
}

// ---------------------------------------------------------------------------
// Busy pin
// ---------------------------------------------------------------------------

bool Inkplate13::is_busy_() {
  return gpio_get_level(PIN(pin_busy_)) != 0;
}

// ---------------------------------------------------------------------------
// GPIO helpers
// ---------------------------------------------------------------------------

void Inkplate13::set_io_pins_() {
  gpio_set_direction(PIN(pin_rst_),    GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN(pin_dc_),     GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN(pin_cs_m_),   GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN(pin_cs_s_),   GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN(pin_busy_),   GPIO_MODE_INPUT);
  gpio_set_pull_mode(PIN(pin_busy_),   GPIO_PULLUP_ONLY);
  gpio_set_direction(PIN(pin_pwr_en_), GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN(pin_bs0_),    GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN(pin_bs1_),    GPIO_MODE_OUTPUT);

  gpio_set_level(PIN(pin_dc_),     1);
  gpio_set_level(PIN(pin_cs_m_),   1);
  gpio_set_level(PIN(pin_cs_s_),   1);
  gpio_set_level(PIN(pin_rst_),    0);
  gpio_set_level(PIN(pin_pwr_en_), 0);
  gpio_set_level(PIN(pin_bs0_),    0);
  gpio_set_level(PIN(pin_bs1_),    1);
}

void Inkplate13::set_all_pins_low_() {
  const gpio_num_t pins[] = {
    PIN(pin_rst_), PIN(pin_dc_),  PIN(pin_cs_m_),
    PIN(pin_cs_s_), PIN(pin_busy_), PIN(pin_pwr_en_),
    PIN(pin_bs0_), PIN(pin_bs1_),
  };
  for (gpio_num_t p : pins) {
    gpio_set_direction(p, GPIO_MODE_OUTPUT);
    gpio_set_level(p, 0);
  }
}

}  // namespace esphome::inkplate_spi
