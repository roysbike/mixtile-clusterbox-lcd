#pragma once

#include <stdint.h>

class TFT_eSPI;

// XPT2046 on the E32R28T's second SPI bus. A resistive panel needs a saved
// calibration; until that exists, read() stays up so a bad map cannot tap a node.
struct TouchSample {
  bool down;
  int16_t x;
  int16_t y;
};

class TouchPanel {
 public:
  void begin();
  bool calibrated() const { return valid_; }
  // Blocks until four corner taps, BOOT is held, or the wait times out.
  bool calibrate(TFT_eSPI &tft);
  TouchSample read();

 private:
  bool load();
  void save();
  bool sample(int &x, int &y, int &z);
  uint16_t readCmd(uint8_t cmd);

  int16_t x_left_ = 0;
  int16_t x_right_ = 0;
  int16_t y_top_ = 0;
  int16_t y_bottom_ = 0;
  uint8_t swap_ = 0;
  bool valid_ = false;
};
