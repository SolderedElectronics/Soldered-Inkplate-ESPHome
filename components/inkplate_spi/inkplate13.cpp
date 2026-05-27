#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "inkplate13.h"

#include "driver/gpio.h"

namespace esphome::inkplate_spi {

static const char *TAG = "inkplate_spi";

// Runtime register addresses (used outside init sequence)
static constexpr uint8_t REG_DTM   = 0x10;
static constexpr uint8_t REG_DRF   = 0x12;
static constexpr uint8_t REG_PON   = 0x04;
static constexpr uint8_t REG_POF   = 0x02;
static constexpr uint8_t REG_PTLW  = 0x83;
static constexpr uint8_t REG_CMD66 = 0xF0;

static constexpr uint8_t REG_DRF_V[]   = {0x00};
static constexpr uint8_t REG_POF_V[]   = {0x00};
static constexpr uint8_t REG_CMD66_V[] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};

// Inkplate 13 Spectra fixed pin mapping
static constexpr gpio_num_t PIN_RST    = GPIO_NUM_4;
static constexpr gpio_num_t PIN_DC     = GPIO_NUM_14;
static constexpr gpio_num_t PIN_BUSY   = GPIO_NUM_7;
static constexpr gpio_num_t PIN_PWR_EN = GPIO_NUM_21;
static constexpr gpio_num_t PIN_CS_M   = GPIO_NUM_42;
static constexpr gpio_num_t PIN_CS_S   = GPIO_NUM_39;
static constexpr gpio_num_t PIN_BS0    = GPIO_NUM_6;
static constexpr gpio_num_t PIN_BS1    = GPIO_NUM_5;

void Inkplate13::setup() {
  ESP_LOGI(TAG, "setup() start");
  initialize_();
  ESP_LOGI(TAG, "setup() done");
}

void Inkplate13::loop() {}

void Inkplate13::update() {
  ESP_LOGI(TAG, "update() start");
  this->do_update_();
  ESP_LOGI(TAG, "update() done");
}

void Inkplate13::dump_config() {
  ESP_LOGCONFIG(TAG, "Inkplate 13 Spectra");
}

void Inkplate13::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x >= get_width_internal() || y >= get_height_internal() || x < 0 || y < 0)
    return;
  uint32_t pos = (x / 2) + y * (get_width_internal() / 2);
  uint8_t current = this->buffer_[pos];
  uint8_t cv = map_color_to_palette_(color);
  if (x % 2 == 0)
    this->buffer_[pos] = (current & 0x0F) | (cv << 4);
  else
    this->buffer_[pos] = (current & 0xF0) | cv;
}

uint8_t Inkplate13::map_color_to_palette_(Color color) {
  uint8_t r = color.red, g = color.green, b = color.blue;
  if (r > 200 && g > 200 && b > 200)              return 0x01;  // WHITE
  if (r < 50  && g < 50  && b < 50)               return 0x00;  // BLACK
  if (r > 150 && g < 100 && b < 100)              return 0x03;  // RED
  if (r < 100 && g > 150 && b < 100)              return 0x06;  // GREEN
  if (r < 100 && g < 100 && b > 150)              return 0x05;  // BLUE
  if (r > 150 && g > 150 && b < 100)              return 0x02;  // YELLOW
  return 0x00;
}

void Inkplate13::initialize_() {
  ESP_LOGI(TAG, "allocating buffer");
  RAMAllocator<uint8_t> allocator;
  size_t buffer_size = (size_t) get_width_internal() * get_height_internal() / 2;

  if (this->buffer_ != nullptr)
    allocator.deallocate(this->buffer_, buffer_size);

  ESP_LOGI(TAG, "calling spi_setup");
  this->spi_setup();
  ESP_LOGI(TAG, "spi_setup done");

  this->buffer_ = allocator.allocate(buffer_size);
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate %zu byte frame buffer", buffer_size);
    return;
  }

  ESP_LOGI(TAG, "buffer allocated (%zu bytes)", buffer_size);
  memset(this->buffer_, 0x11, buffer_size);  // 0x11 = white
}

void Inkplate13::send_command_(uint8_t cmd, const uint8_t *data, size_t len, ChipId chip) {
  if (chip & CHIP_MASTER) gpio_set_level(PIN_CS_M, 0);
  if (chip & CHIP_SLAVE)  gpio_set_level(PIN_CS_S, 0);

  this->enable();
  this->write_byte(cmd);
  if (len > 0 && data != nullptr)
    this->write_array(data, len);
  this->disable();

  if (chip & CHIP_MASTER) gpio_set_level(PIN_CS_M, 1);
  if (chip & CHIP_SLAVE)  gpio_set_level(PIN_CS_S, 1);
}

void Inkplate13::display(bool leave_on) {
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "display() called with no buffer — PSRAM missing?");
    return;
  }
  ESP_LOGI(TAG, "display() start");
  set_panel_state_(true);
  ESP_LOGI(TAG, "panel on, sending master data");

  const size_t rows          = get_height_internal();
  const size_t bytes_per_row = get_width_internal() / 2;
  const size_t half          = bytes_per_row / 2;

  gpio_set_level(PIN_CS_M, 0);
  this->enable();
  this->write_byte(REG_DTM);
  for (size_t i = 0; i < rows; i++)
    this->write_array(this->buffer_ + i * bytes_per_row, half);
  this->disable();
  gpio_set_level(PIN_CS_M, 1);
  ESP_LOGI(TAG, "master data sent, waiting busy");

  wait_for_busy_();
  ESP_LOGI(TAG, "sending slave data");

  gpio_set_level(PIN_CS_S, 0);
  this->enable();
  this->write_byte(REG_DTM);
  for (size_t i = 0; i < rows; i++)
    this->write_array(this->buffer_ + i * bytes_per_row + half, half);
  this->disable();
  gpio_set_level(PIN_CS_S, 1);
  ESP_LOGI(TAG, "slave data sent, waiting busy");

  wait_for_busy_();
  ESP_LOGI(TAG, "sending DRF");

  send_command_(REG_DRF, REG_DRF_V, sizeof(REG_DRF_V), CHIP_BOTH);
  wait_for_busy_();
  ESP_LOGI(TAG, "display() done");

  if (!leave_on)
    set_panel_state_(false);
}


void Inkplate13::display_partial(int x, int y, int w, int h, bool leave_on) {
  if (this->buffer_ == nullptr) return;

  // Clip to logical screen bounds
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > (int) this->get_width())  w = (int) this->get_width()  - x;
  if (y + h > (int) this->get_height()) h = (int) this->get_height() - y;
  if (w <= 0 || h <= 0) return;

  const int16_t W = (int16_t) get_width_internal();   // 1200
  const int16_t H = (int16_t) get_height_internal();  // 1600

  // Map logical (rotated) region → physical (panel-native) col/row
  int16_t colStart, colEnd, rowStart, rowEnd;
  switch ((int) this->rotation_) {
    case 90:
      colStart = W - y - h;  colEnd = W - 1 - y;
      rowStart = x;          rowEnd = x + w - 1;
      break;
    case 180:
      colStart = x;          colEnd = x + w - 1;
      rowStart = y;          rowEnd = y + h - 1;
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

  // PTLW alignment: cols → multiples of 4, rows → even
  colStart = (colStart / 4) * 4;
  colEnd   = (((colEnd + 4) / 4) * 4) - 1;
  if (colEnd   >= W) colEnd   = W - 1;
  if (rowStart %  2) rowStart--;
  if (rowStart <  0) rowStart = 0;
  if ((rowEnd + 1) % 2) rowEnd++;
  if (rowEnd   >= H) rowEnd   = H - 1;

  ESP_LOGI(TAG, "display_partial col=%d-%d row=%d-%d", colStart, colEnd, rowStart, rowEnd);
  set_panel_state_(true);

  const int16_t HALF_W     = W / 2;    // 600 px per chip
  const int16_t HALF_BYTES = HALF_W / 2;  // 300 bytes per row per chip

  bool masterNeeded = (colStart < HALF_W);
  bool slaveNeeded  = (colEnd  >= HALF_W);

  static const uint8_t ptlwNull[9] = {
    0x00, 0x00,  // HRST = 0
    0x00, 0x07,  // HRED = 7
    0x00, 0x00,  // VRST = 0
    0x00, 0x01,  // VRED = 1
    0x01         // PT   = 1
  };

  auto send_ptlw_dtm = [&](bool isMaster, bool needed) {
    gpio_num_t cs_pin = isMaster ? PIN_CS_M : PIN_CS_S;
    ChipId chip = isMaster ? CHIP_MASTER : CHIP_SLAVE;

    uint8_t ptlwData[9];
    int16_t bytesPerRow, memColOff, rStart, rEnd;

    if (needed) {
      int16_t lcs = isMaster ? colStart : ((colStart >= HALF_W) ? colStart - HALF_W : 0);
      int16_t lce = isMaster
                    ? ((colEnd < HALF_W) ? colEnd : HALF_W - 1)
                    : (colEnd - HALF_W);
      uint16_t HRST = (uint16_t) lcs * 2;
      uint16_t HRED = (uint16_t)(lce + 1) * 2 - 1;
      uint16_t VRST = (uint16_t) rowStart / 2;
      uint16_t VRED = (uint16_t)(rowEnd + 1) / 2 - 1;
      ptlwData[0] = HRST >> 8; ptlwData[1] = HRST & 0xFF;
      ptlwData[2] = HRED >> 8; ptlwData[3] = HRED & 0xFF;
      ptlwData[4] = VRST >> 8; ptlwData[5] = VRST & 0xFF;
      ptlwData[6] = VRED >> 8; ptlwData[7] = VRED & 0xFF;
      ptlwData[8] = 0x01;
      bytesPerRow = (lce - lcs + 1) / 2;
      memColOff   = isMaster ? (lcs / 2) : (HALF_BYTES + lcs / 2);
      rStart = rowStart; rEnd = rowEnd;
    } else {
      memcpy(ptlwData, ptlwNull, 9);
      bytesPerRow = 2;
      memColOff   = isMaster ? 0 : HALF_BYTES;
      rStart = 0; rEnd = 3;
    }

    send_command_(REG_CMD66, REG_CMD66_V, sizeof(REG_CMD66_V), chip);

    gpio_set_level(cs_pin, 0);
    this->enable();
    this->write_byte(REG_PTLW);
    this->write_array(ptlwData, 9);
    this->disable();
    gpio_set_level(cs_pin, 1);

    gpio_set_level(cs_pin, 0);
    this->enable();
    this->write_byte(REG_DTM);
    for (int16_t row = rStart; row <= rEnd; row++)
      this->write_array(this->buffer_ + row * (W / 2) + memColOff, bytesPerRow);
    this->disable();
    gpio_set_level(cs_pin, 1);
  };

  send_ptlw_dtm(true,  masterNeeded);
  wait_for_busy_();
  send_ptlw_dtm(false, slaveNeeded);
  wait_for_busy_();

  send_command_(REG_DRF, REG_DRF_V, sizeof(REG_DRF_V), CHIP_BOTH);
  wait_for_busy_();
  ESP_LOGI(TAG, "display_partial done");

  if (!leave_on) set_panel_state_(false);
}

void Inkplate13::set_panel_state_(bool state) {
  ESP_LOGI(TAG, "set_panel_state_(%d), current=%d", state, this->panel_state_);
  if (state == this->panel_state_)
    return;

  if (state) {
    ESP_LOGI(TAG, "pins to low");
    set_panel_pins_to_low_();
    delay(50);
    ESP_LOGI(TAG, "set_io");
    set_io_();
    gpio_set_level(PIN_PWR_EN, 1);
    delay(100);
    ESP_LOGI(TAG, "reset panel");
    reset_panel_();
    delay(100);
    ESP_LOGI(TAG, "screen_init");
    screen_init_();
    ESP_LOGI(TAG, "PON");
    send_command_(REG_PON, nullptr, 0, CHIP_BOTH);
    ESP_LOGI(TAG, "wait busy after PON");
    wait_for_busy_();
    ESP_LOGI(TAG, "panel on done");
  } else {
    send_command_(REG_POF, REG_POF_V, sizeof(REG_POF_V), CHIP_BOTH);
    wait_for_busy_();
    gpio_set_direction(PIN_DC,     GPIO_MODE_INPUT);
    gpio_set_direction(PIN_CS_M,   GPIO_MODE_INPUT);
    gpio_set_direction(PIN_CS_S,   GPIO_MODE_INPUT);
    gpio_set_direction(PIN_RST,    GPIO_MODE_INPUT);
    gpio_set_direction(PIN_BUSY,   GPIO_MODE_INPUT);
    gpio_set_direction(PIN_PWR_EN, GPIO_MODE_INPUT);
    gpio_set_level(PIN_PWR_EN, 0);
  }

  this->panel_state_ = state;
}

bool Inkplate13::set_panel_deep_sleep_(bool state) {
  if (state) {
    send_command_(REG_POF, REG_POF_V, sizeof(REG_POF_V), CHIP_BOTH);
    wait_for_busy_();
    gpio_set_level(PIN_RST, 0);
    this->panel_state_ = false;
  } else {
    set_panel_pins_to_low_();
    delay(50);
    set_io_();
    gpio_set_level(PIN_PWR_EN, 1);
    delay(100);
    reset_panel_();
    delay(100);
    screen_init_();
    send_command_(REG_PON, nullptr, 0, CHIP_BOTH);
    wait_for_busy_();
    this->panel_state_ = true;
  }
  return true;
}

void Inkplate13::screen_init_() {
  // Replay init sequence stored by set_init_sequence().
  // Wire format per entry: [chip, cmd, n_data, data_0 ... data_(n-1)]
  size_t i = 0;
  while (i < init_seq_.size()) {
    uint8_t chip = init_seq_[i++];
    uint8_t cmd  = init_seq_[i++];
    uint8_t n    = init_seq_[i++];
    send_command_(cmd, init_seq_.data() + i, n, static_cast<ChipId>(chip));
    i += n;
  }
}

void Inkplate13::set_io_() {
  gpio_set_direction(PIN_RST,    GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_DC,     GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_CS_M,   GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_CS_S,   GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_BUSY,   GPIO_MODE_INPUT);
  gpio_set_pull_mode(PIN_BUSY,   GPIO_PULLUP_ONLY);
  gpio_set_direction(PIN_PWR_EN, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_BS0,    GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_BS1,    GPIO_MODE_OUTPUT);

  gpio_set_level(PIN_DC,     1);
  gpio_set_level(PIN_CS_M,   1);
  gpio_set_level(PIN_CS_S,   1);
  gpio_set_level(PIN_RST,    0);
  gpio_set_level(PIN_PWR_EN, 0);
  gpio_set_level(PIN_BS0,    0);
  gpio_set_level(PIN_BS1,    1);
}

void Inkplate13::set_panel_pins_to_low_() {
  gpio_set_direction(PIN_RST,    GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_DC,     GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_CS_M,   GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_CS_S,   GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_BUSY,   GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_PWR_EN, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_BS0,    GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_BS1,    GPIO_MODE_OUTPUT);

  gpio_set_level(PIN_RST,    0);
  gpio_set_level(PIN_DC,     0);
  gpio_set_level(PIN_CS_M,   0);
  gpio_set_level(PIN_CS_S,   0);
  gpio_set_level(PIN_BUSY,   0);
  gpio_set_level(PIN_PWR_EN, 0);
  gpio_set_level(PIN_BS0,    0);
  gpio_set_level(PIN_BS1,    0);
}

void Inkplate13::reset_panel_() {
  gpio_set_level(PIN_RST, 0);
  delay(100);
  gpio_set_level(PIN_RST, 1);
  delay(100);
}

void Inkplate13::wait_for_busy_() {
  uint32_t start = millis();
  while (!gpio_get_level(PIN_BUSY)) {
    if (millis() - start > 45000) {
      ESP_LOGE(TAG, "wait_for_busy_ timeout");
      return;
    }
    delay(1);
    App.feed_wdt();
  }
}

}  // namespace esphome::inkplate_spi
