#pragma once

#include "inkplate.h"
#include "inkplate_common.h"

namespace esphome::inkplate {

// Source: Inkplate10Driver.cpp / waveforms.h — waveform1 (default WAVEFORM3BIT)
static const uint8_t INKPLATE10_WAVEFORM3BIT[8][9] = {
    {0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0, 0, 0, 2, 2, 2, 1, 1, 0},
    {0, 0, 2, 1, 1, 2, 2, 1, 0},
    {0, 1, 2, 2, 1, 2, 2, 1, 0},
    {0, 0, 2, 1, 2, 2, 2, 1, 0},
    {0, 2, 2, 2, 2, 2, 2, 1, 0},
    {0, 0, 0, 0, 0, 2, 1, 2, 0},
    {0, 0, 0, 2, 2, 2, 2, 2, 0},
};

class Inkplate10 : public InkplateParallelBase {
 public:
  Inkplate10(int width, int height, int dark_phases, int partial_phases, int grayscale_phases)
      : InkplateParallelBase(width, height, dark_phases, partial_phases, grayscale_phases) {
    this->clean_seq_     = CLEAN_SEQ_1B;
    this->clean_seq_len_ = CLEAN_SEQ_LEN;
  }

  void setup() override;
  void dump_config() override;

 protected:
  // Inkplate10 uses separate clean sequences for 1b and 3b paths.
  // Source: Inkplate10Driver.cpp display1b() / display3b()
  static const CleanStep CLEAN_SEQ_1B[8];
  static const CleanStep CLEAN_SEQ_3B[8];
  static constexpr size_t CLEAN_SEQ_LEN = 8;

  uint8_t clean_data_byte_() const override;
};

}  // namespace esphome::inkplate
