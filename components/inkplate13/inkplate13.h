#pragma once

#include "esphome/core/component.h"

namespace esphome::inkplate13 {

class Inkplate13 : public Component {
public:
  void setup() override;
  void loop() override;
  void dump_config() override;
protected:
  void test();
};
}
