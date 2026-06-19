#pragma once

#include "inkplate.h"
#include "inkplate_common.h"

namespace esphome::inkplate {

// Source: InkplateLibrary Waveforms_Inkplate5.h WAVEFORM3BIT
static const uint8_t INKPLATE5_WAVEFORM3BIT[8][9] = {
    {0, 0, 1, 1, 2, 1, 1, 1, 0},
    {1, 1, 2, 2, 1, 2, 1, 1, 0},
    {0, 1, 2, 2, 1, 1, 2, 1, 0},
    {0, 0, 1, 1, 1, 1, 1, 2, 0},
    {1, 2, 1, 2, 1, 1, 1, 2, 0},
    {0, 1, 1, 1, 2, 0, 1, 2, 0},
    {1, 1, 1, 2, 2, 2, 1, 2, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0},
};

class Inkplate5 : public InkplateParallelBase {
 public:
  Inkplate5(int width, int height, int dark_phases, int partial_phases, int grayscale_phases)
      : InkplateParallelBase(width, height, dark_phases, partial_phases, grayscale_phases) {
    this->clean_seq_     = CLEAN_SEQ;
    this->clean_seq_len_ = CLEAN_SEQ_LEN;
  }

  void setup() override;
  void dump_config() override;

 protected:
  // Source: Inkplate5Driver.cpp EPDDriver::display1b() / display3b()
  static const CleanStep CLEAN_SEQ[8];
  static constexpr size_t CLEAN_SEQ_LEN = 8;

  bool do_board_transfer_step_() override;
};

}  // namespace esphome::inkplate
