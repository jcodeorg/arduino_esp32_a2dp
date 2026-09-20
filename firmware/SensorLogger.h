#pragma once

#include <Arduino.h>

class BLECharacteristic;
class BLEServer;

struct SensorReading {
  int64_t timestamp;
  float temperature;
  float humidity;
  float illuminance;
  uint16_t soilMoisture;
};

class SensorLogger {
public:
  static constexpr size_t kMaxLogEntries = 2000;

  void begin(uint8_t soilPin);
  bool update();
  void refreshSensors();
  const SensorReading& latest() const;
  bool hasReading() const;
  size_t logCount() const;
  bool getCurrentTime(char* buffer, size_t bufferSize) const;
  bool formatTimestamp(int64_t timestamp, char* buffer, size_t bufferSize) const;
  const char* bluetoothName() const;
  bool isBluetoothConnected() const;

private:
  static constexpr unsigned long kMeasureIntervalMs = 3600000UL;
  static constexpr unsigned long kAdvertiseRetryIntervalMs = 500UL;

  void readAndStore();
  void processBluetooth();
  void processCommand(const String& command);
  void receiveBluetoothData(const String& data);
  void sendNotification(const char* data);
  void sendLog();
  void clearLog();
  void applyHistoricalTimeAdjustment(int64_t epoch);
  int64_t currentTimestamp() const;

  BLEServer* server_ = nullptr;
  BLECharacteristic* txCharacteristic_ = nullptr;
  SensorReading log_[kMaxLogEntries] = {};
  SensorReading latest_ = {};
  size_t logStart_ = 0;
  size_t logCount_ = 0;
  uint8_t soilPin_ = 0;
  unsigned long lastMeasure_ = 0;
  unsigned long lastAdvertise_ = 0;
  int64_t epochOffset_ = 0;
  int64_t historicalEpochOffset_ = 0;
  bool timeSynchronized_ = false;
  bool historicalAdjustmentApplied_ = false;
  bool bluetoothConnected_ = false;
  String bluetoothName_;
  String commandBuffer_;

  friend class SensorLoggerCallbacks;
  friend class SensorLoggerServerCallbacks;
};
