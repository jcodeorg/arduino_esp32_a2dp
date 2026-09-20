#pragma once

#include <Arduino.h>

class BLECharacteristic;
class BLEServer;

struct SensorReading {
  unsigned long timestamp;
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
  const SensorReading& latest() const;
  bool hasReading() const;

private:
  static constexpr unsigned long kMeasureIntervalMs = 3600000UL;

  void readAndStore();
  void processBluetooth();
  void processCommand(const String& command);
  void receiveBluetoothData(const String& data);
  void sendNotification(const char* data);
  void sendLog();
  void clearLog();
  unsigned long currentTimestamp() const;

  BLEServer* server_ = nullptr;
  BLECharacteristic* txCharacteristic_ = nullptr;
  SensorReading log_[kMaxLogEntries] = {};
  SensorReading latest_ = {};
  size_t logStart_ = 0;
  size_t logCount_ = 0;
  uint8_t soilPin_ = 0;
  unsigned long lastMeasure_ = 0;
  unsigned long epochOffset_ = 0;
  bool timeSynchronized_ = false;
  String bluetoothName_;
  String commandBuffer_;

  friend class SensorLoggerCallbacks;
};
