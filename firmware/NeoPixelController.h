#pragma once

#include <Arduino.h>

class NeoPixelController {
public:
  void begin();
  void setPlaying(bool playing);
  void update();

private:
  static constexpr uint8_t kStoppedColorIndex = 3;
  static constexpr unsigned long kUpdateIntervalMs = 30;
  static constexpr unsigned long kTransitionMs = 3000;

  bool isPlaying_ = false;
  uint8_t colorIndex_ = 0;
  unsigned long lastUpdate_ = 0;
  unsigned long transitionStart_ = 0;
};