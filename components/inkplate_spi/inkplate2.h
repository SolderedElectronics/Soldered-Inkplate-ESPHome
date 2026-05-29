#pragma once

#include "inkplate_spi.h"

namespace esphome::inkplate_spi {

// Inkplate2 — driver for the 2" 3-color (black/white/red) panel.
//
// Single-chip, single CS pin. EPD controller connected directly to ESP32 GPIO.
// No partial update support.
//
// Init order differs from Inkplate6Color: PON (0x04) must be sent BEFORE panel
// settings. Full power-on sequence is handled inside do_power_on_step_() so
// the state machine's INIT step (which fires between POWER_ON and PON) is a
// no-op with an empty init_sequence.
//
// Internal color indices: WHITE=1 (matches buffer init 0x11), BLACK=0, RED=2.
// Transfer sends two 1bpp planes derived on-the-fly from the 4bpp buffer.
class Inkplate2 : public InkplateBase {
 public:
  Inkplate2(int width, int height) : InkplateBase(width, height) {}

  void dump_config() override;

  void set_pin_cs(GPIOPin *p) { this->pin_cs_ = p; }

 protected:
  static constexpr size_t ROWS_PER_CHUNK = 8;

  // Power-on: RST pulse → PON(0x04) → BUSY wait + 200ms → panel settings → 20ms
  // Matches Arduino: resetPanel() → sendCommand(0x04) → waitForEpd() → settings → delay(20)
  enum PowerOnSub {
    PON_SETUP,
    PON_RST_LOW_WAIT,      // 100ms RST low (reset pulse)
    PON_RST_HIGH_WAIT,     // 100ms RST high, then send PON(0x04)
    PON_WAIT_PON_BUSY,     // wait BUSY HIGH after PON
    PON_PON_BUSY_DELAY,    // 200ms settle (from Arduino waitForEpd delay(200))
    PON_SEND_SETTINGS,     // send 0x00 + 0x61 + 0x50 — blocking OK, ~10 bytes total
    PON_BOOT_DELAY,        // 20ms (from Arduino display() delay after wakeup)
    PON_DONE,
  };

  // Transfer: DTM1 BW plane (0x10) → DTM2 RED plane (0x13) → data stop (0x11)
  // Converts 4bpp nibble-packed buffer to two 1bpp planes on-the-fly, 8 rows/tick.
  enum TransferSub {
    TRF_SEND_BW,           // send 0x10 cmd (DC low), open data CS transaction (DC high)
    TRF_STREAM_BW,         // stream BW plane rows; close CS when done
    TRF_SEND_RED,          // send 0x13 cmd, open data CS transaction
    TRF_STREAM_RED,        // stream RED plane rows; close CS when done
    TRF_SEND_STOP,         // send 0x11 + 0x00
    TRF_DONE,
  };

  // Power-off: update VCOM → POF(0x02) → BUSY wait → 200ms settle
  enum PowerOffSub {
    POFF_SEND_VCOM,        // send 0x50 + 0xF7
    POFF_SEND_POF,         // send 0x02
    POFF_WAIT,             // wait BUSY HIGH
    POFF_DELAY,            // 200ms (from Arduino waitForEpd delay(200))
    POFF_DONE,
  };

  uint8_t map_color_to_index_(Color color) override;
  void    send_command_to_chip_(uint8_t cmd, const uint8_t *data, size_t len, uint8_t chip) override;
  void    prepare_for_update_() override;
  bool do_power_on_step_()  override;
  bool do_send_pon_()       override { return true; }  // no-op: PON sent inside do_power_on_step_()
  bool do_transfer_step_()  override;
  void do_send_refresh_()   override;
  bool do_power_off_step_() override;
  bool is_busy_()           override;
  bool do_deep_sleep_()     override;
  void do_emergency_off_()  override;

  GPIOPin    *pin_cs_{nullptr};
  PowerOnSub  pon_sub_{PON_SETUP};
  TransferSub trf_sub_{TRF_SEND_BW};
  PowerOffSub poff_sub_{POFF_SEND_VCOM};
  uint32_t    sub_start_ms_{0};
  size_t      trf_row_{0};
};

}  // namespace esphome::inkplate_spi
