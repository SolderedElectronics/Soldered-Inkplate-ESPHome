#pragma once

#include "inkplate.h"
#include "inkplate_common.h"

namespace esphome::inkplate {

// 3-bit grayscale waveform: [intensity_0-7][phase_0-8].
// Values: 0=discharge, 1=dark, 2=light. Identical for V1 and V2.
// Source: InkplateLibrary Waveforms_Inkplate6.h WAVEFORM3BIT
static const uint8_t INKPLATE6_WAVEFORM3BIT[8][9] = {
    {0, 0, 0, 0, 1, 1, 1, 1, 0},
    {0, 0, 0, 1, 1, 1, 1, 0, 0},
    {1, 1, 1, 1, 0, 2, 1, 0, 0},
    {1, 1, 1, 2, 2, 1, 1, 0, 0},
    {1, 1, 1, 1, 2, 2, 1, 0, 0},
    {0, 1, 1, 1, 2, 2, 1, 0, 0},
    {0, 0, 0, 0, 1, 1, 2, 0, 0},
    {0, 0, 0, 0, 0, 0, 2, 0, 0},
};

class Inkplate6 : public InkplateParallelBase {
 public:
  Inkplate6(int width, int height, int dark_phases, int partial_phases, int grayscale_phases)
      : InkplateParallelBase(width, height, dark_phases, partial_phases, grayscale_phases) {
    this->clean_seq_     = CLEAN_SEQ;
    this->clean_seq_len_ = CLEAN_SEQ_LEN;
  }

  void setup() override;
  void dump_config() override;

 protected:
  // Source: Inkplate6Driver.cpp EPDDriver::display1b()
  static const CleanStep CLEAN_SEQ[9];
  static constexpr size_t CLEAN_SEQ_LEN = 9;
};

}  // namespace esphome::inkplate
