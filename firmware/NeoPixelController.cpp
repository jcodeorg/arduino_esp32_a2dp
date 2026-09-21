#include "NeoPixelController.h"

#include <Adafruit_NeoPixel.h>

namespace {
// 今回は1個のNeoPixelを使います。
constexpr uint16_t kCount = 1;
// 音楽再生中に順番に表示する色です。値はRGBの順です。
constexpr uint32_t kColors[] = {
  0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00,
  0x0000FF, 0x4B0082, 0x8F00FF
};
constexpr uint8_t kColorCount = sizeof(kColors) / sizeof(kColors[0]);

Adafruit_NeoPixel pixels(kCount, 0, NEO_GRB + NEO_KHZ800);
}

void NeoPixelController::begin(uint8_t pin) {
  // LEDの接続先と明るさを設定し、停止中の色を表示します。
  pixels.setPin(pin);
  pixels.begin();
  pixels.setBrightness(255);
  pixels.setPixelColor(0, kColors[kStoppedColorIndex]);
  pixels.show();
}

void NeoPixelController::setPlaying(bool playing) {
  isPlaying_ = playing;

  if (isPlaying_) {
    // 再生開始時刻を記録します。ここから色の変化を始めます。
    transitionStart_ = millis();
    lastUpdate_ = transitionStart_;
    return;
  }

  // 停止中は、決めておいた色に戻します。
  colorIndex_ = kStoppedColorIndex;
  pixels.setPixelColor(0, kColors[colorIndex_]);
  pixels.show();
}

void NeoPixelController::update() {
  // 再生中でない、または更新間隔が短すぎるときは何もしません。
  if (!isPlaying_ || millis() - lastUpdate_ < kUpdateIntervalMs) {
    return;
  }

  unsigned long now = millis();
  lastUpdate_ = now;
  unsigned long transitionElapsed = now - transitionStart_;
  if (transitionElapsed >= kTransitionMs) {
    // 色の切り替えが終わったら、次の色へ進みます。
    colorIndex_ = (colorIndex_ + transitionElapsed / kTransitionMs) % kColorCount;
    transitionStart_ = now;
    transitionElapsed = 0;
  }

  uint8_t nextColorIndex = (colorIndex_ + 1) % kColorCount;
  uint32_t currentColor = kColors[colorIndex_];
  uint32_t nextColor = kColors[nextColorIndex];
  // 現在の色と次の色を混ぜることで、急に変わらず滑らかに変化させます。
  uint8_t red = ((currentColor >> 16) * (kTransitionMs - transitionElapsed)
    + (nextColor >> 16) * transitionElapsed) / kTransitionMs;
  uint8_t green = (((currentColor >> 8) & 0xFF) * (kTransitionMs - transitionElapsed)
    + ((nextColor >> 8) & 0xFF) * transitionElapsed) / kTransitionMs;
  uint8_t blue = ((currentColor & 0xFF) * (kTransitionMs - transitionElapsed)
    + (nextColor & 0xFF) * transitionElapsed) / kTransitionMs;
  pixels.setPixelColor(0, red, green, blue);
  pixels.show();
}