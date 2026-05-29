#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "inkplate_spi.h"

namespace esphome::inkplate_spi {

static const char *TAG = "inkplate_spi";

// ---------------------------------------------------------------------------
// Setup / loop / update
// ---------------------------------------------------------------------------

void InkplateBase::setup() {
  this->spi_setup();

  size_t buf_size = (size_t) this->width_ * this->height_ / 2;
  RAMAllocator<uint8_t> allocator;
  this->buffer_ = allocator.allocate(buf_size);
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Buffer alloc failed (%zu bytes)", buf_size);
    return;
  }
  uint8_t w = this->white_color_index_();
  memset(this->buffer_, (uint8_t)((w << 4) | w), buf_size);  // fill both nibbles with white index
  ESP_LOGI(TAG, "setup() done — buffer %zu bytes", buf_size);
  this->disable_loop();  // loop() off until first update
}

void InkplateBase::loop() {
  // Each call does one small unit of work and returns immediately.
  // This keeps the ESPHome main loop responsive — WiFi, sensors, OTA
  // all continue running normally during multi-second panel refreshes.
  this->process_state_();
}

void InkplateBase::update() {
  if (this->state_ != STATE_IDLE) {
    ESP_LOGW(TAG, "update() skipped — display busy (state %d)", (int) this->state_);
    return;
  }
  this->do_update_();  // run user lambda → draw into buffer_

  this->update_count_++;
  this->partial_ = false;  // update() always does full refresh; use display_partial() for subregion
  ESP_LOGD(TAG, "update #%d — full refresh", this->update_count_);

  this->prepare_for_update_();  // let subclass reset its sub-states
  this->enable_loop();
  this->set_state_(STATE_POWER_ON);
}

void InkplateBase::dump_config() {
  ESP_LOGCONFIG(TAG, "Inkplate SPI %dx%d", this->width_, this->height_);
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void InkplateBase::on_safe_shutdown() {
  if (this->state_ == STATE_IDLE) return;  // panel already in deep sleep — nothing to do
  ESP_LOGW(TAG, "on_safe_shutdown() mid-refresh — emergency power off");
  this->state_ = STATE_IDLE;
  this->disable_loop();
  this->do_emergency_off_();  // subclass kills pwr_en / drives RST low
}

// ---------------------------------------------------------------------------
// Pixel drawing
// ---------------------------------------------------------------------------

void InkplateBase::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= this->width_ || y >= this->height_)
    return;
  // 4bpp packed: 2 pixels per byte.
  // High nibble (bits 7-4) holds the even-x pixel, low nibble (bits 3-0) the odd-x pixel.
  uint32_t pos = (uint32_t)(x / 2) + (uint32_t) y * (this->width_ / 2);
  // clamp to nibble — overflow silently corrupts adjacent pixel
  uint8_t  cv  = this->map_color_to_index_(color) & 0x0F;
  if (x % 2 == 0)
    this->buffer_[pos] = (this->buffer_[pos] & 0x0F) | (cv << 4);
  else
    this->buffer_[pos] = (this->buffer_[pos] & 0xF0) | cv;
}

// ---------------------------------------------------------------------------
// Default init sequence replay
// ---------------------------------------------------------------------------

void InkplateBase::do_init_() {
  // Wire format: [chip, cmd, n_data, data_0 ... data_(n-1)]
  size_t i = 0;
  while (i < this->init_seq_len_) {
    uint8_t chip = this->init_seq_[i++];
    uint8_t cmd  = this->init_seq_[i++];
    uint8_t n    = this->init_seq_[i++];
    this->send_command_to_chip_(cmd, this->init_seq_ + i, n, chip);
    i += n;
  }
}

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------

void InkplateBase::set_state_(State s) {
  ESP_LOGD(TAG, "state %d → %d", (int) this->state_, (int) s);
  this->state_          = s;
  this->state_start_ms_ = App.get_loop_component_start_time();
  if (s == STATE_IDLE) this->disable_loop();
}

void InkplateBase::process_state_() {
  switch (this->state_) {

    case STATE_IDLE:
      return;

    case STATE_POWER_ON:
      if (!this->do_power_on_step_()) return;   // subclass not done yet
      this->set_state_(STATE_INIT);
      break;

    case STATE_INIT:
      this->do_init_();
      this->set_state_(STATE_PON);
      break;

    case STATE_PON:
      if (!this->do_send_pon_()) return;
      this->set_state_(STATE_WAIT_PON);
      break;

    case STATE_WAIT_PON:
      if (!this->is_busy_()) return;            // panel still busy
      this->set_state_(STATE_TRANSFER);
      break;

    case STATE_TRANSFER:
      if (!this->do_transfer_step_()) return;   // subclass not done yet
      this->set_state_(STATE_REFRESH);
      break;

    case STATE_REFRESH:
      this->do_send_refresh_();
      this->set_state_(STATE_WAIT_REFRESH);
      break;

    case STATE_WAIT_REFRESH:
      if (!this->is_busy_()) return;            // panel still refreshing
      this->set_state_(STATE_POWER_OFF);
      break;

    case STATE_POWER_OFF:
      if (!this->do_power_off_step_()) return;  // subclass not done yet
      this->set_state_(STATE_DEEP_SLEEP);
      break;

    case STATE_DEEP_SLEEP:
      if (!this->do_deep_sleep_()) return;
      this->set_state_(STATE_IDLE);
      break;
  }

  App.feed_wdt();
}

// ---------------------------------------------------------------------------
// Partial update entry point (callable from subclass public API)
// ---------------------------------------------------------------------------

void InkplateBase::start_partial_update_(int x, int y, int w, int h) {
  if (this->state_ != STATE_IDLE) {
    ESP_LOGW(TAG, "start_partial_update_() skipped — busy (state %d)", (int) this->state_);
    return;
  }
  // Caller must draw updated pixels into buffer_ before calling this.
  // Coordinates are in logical (rotation-aware) space, same as the drawing API.
  // The subclass maps them to physical panel coords in prepare_for_update_().
  this->partial_x_ = x;
  this->partial_y_ = y;
  this->partial_w_ = w;
  this->partial_h_ = h;
  this->partial_ = true;
  this->prepare_for_update_();
  this->enable_loop();
  this->set_state_(STATE_POWER_ON);
}

}  // namespace esphome::inkplate_spi
