#pragma once

#include <Arduino.h>

class NeoPixelController {
public:
  // NeoPixelを使い始める準備をします。
  void begin(uint8_t pin);
  // 音楽再生中かどうかを設定します。
  void setPlaying(bool playing);
  // 時間の経過に合わせてLEDを更新します。
  void update();

private:
  // 音楽停止中に表示する色（配列の4番目、緑）です。
  static constexpr uint8_t kStoppedColorIndex = 3;
  // 色を計算する間隔です。短いほどなめらかになります。
  static constexpr unsigned long kUpdateIntervalMs = 30;
  // 1色から次の色へ変わる時間です。
  static constexpr unsigned long kTransitionMs = 3000;

  // 現在のLEDの状態を保存します。
  bool isPlaying_ = false;
  uint8_t colorIndex_ = 0;
  unsigned long lastUpdate_ = 0;
  unsigned long transitionStart_ = 0;
};