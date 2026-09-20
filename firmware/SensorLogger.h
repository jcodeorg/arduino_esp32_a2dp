#pragma once

#include <Arduino.h>

class BLECharacteristic;
class BLEServer;

struct SensorReading {
  int64_t timestamp; // 計測した時刻（秒）
  float temperature; // 温度（℃）
  float humidity; // 湿度（%）
  float illuminance; // 明るさ（lux）
  uint16_t soilMoisture; // 土壌センサーの値
};

class SensorLogger {
public:
  static constexpr size_t kMaxLogEntries = 2000;

  // センサー、BLE、保存データを使い始める準備をします。
  void begin(uint8_t soilPin);
  // BLEの受信と、一定時間ごとの計測を行います。
  bool update();
  // センサーを読み取り、画面表示用の最新値を更新します。
  void refreshSensors();
  // 最後に読み取ったセンサー値を返します。
  const SensorReading& latest() const;
  // 1件以上のログが保存されているかを返します。
  bool hasReading() const;
  // 保存されているログの件数を返します。
  size_t logCount() const;
  // 現在時刻を文字列にしてbufferへ書き込みます。
  bool getCurrentTime(char* buffer, size_t bufferSize) const;
  // 秒数を年月日と時刻の文字列に変換します。
  bool formatTimestamp(int64_t timestamp, char* buffer, size_t bufferSize) const;
  // BLEで表示する名前を返します。
  const char* bluetoothName() const;
  // BLE機器が接続中かを返します。
  bool isBluetoothConnected() const;

private:
  // センサーを読み取る間隔（現在は1時間）です。
  static constexpr unsigned long kMeasureIntervalMs = 3600000UL;
  // BLE接続がないとき、広告を再開する間隔です。
  static constexpr unsigned long kAdvertiseRetryIntervalMs = 500UL;

  // 内部処理用の関数です。
  void readAndStore();
  void processBluetooth();
  void processCommand(const String& command);
  void receiveBluetoothData(const String& data);
  void sendNotification(const char* data);
  void sendLog();
  void clearLog();
  void applyHistoricalTimeAdjustment(int64_t epoch);
  int64_t currentTimestamp() const;

  BLEServer* server_ = nullptr; // BLEサーバー本体
  BLECharacteristic* txCharacteristic_ = nullptr; // アプリへ通知する通路
  SensorReading log_[kMaxLogEntries] = {}; // 最大2000件の計測ログ
  SensorReading latest_ = {}; // 最新の計測値
  size_t logStart_ = 0; // 一番古いログの位置
  size_t logCount_ = 0; // 現在のログ件数
  uint8_t soilPin_ = 0; // 土壌センサーを接続したGPIO番号
  unsigned long lastMeasure_ = 0; // 最後に計測した時刻
  unsigned long lastAdvertise_ = 0; // 最後にBLE広告を開始した時刻
  int64_t epochOffset_ = 0; // 現在時刻を合わせるための差
  int64_t historicalEpochOffset_ = 0; // 過去ログに加える時刻の差
  bool timeSynchronized_ = false; // 時刻合わせが済んだか
  bool historicalAdjustmentApplied_ = false; // 過去ログの補正が済んだか
  bool bluetoothConnected_ = false; // BLE接続中か
  String bluetoothName_; // BLEに表示する名前
  String commandBuffer_; // BLEから受け取った未処理の文字

  friend class SensorLoggerCallbacks;
  friend class SensorLoggerServerCallbacks;
};
