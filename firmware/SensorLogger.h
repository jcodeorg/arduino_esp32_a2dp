#pragma once

#include <Arduino.h>
#include <BluetoothSerial.h>

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

  void begin(uint8_t soilPin, const char* bluetoothName);
  bool update();
  const SensorReading& latest() const;
  bool hasReading() const;

private:
  static constexpr unsigned long kMeasureIntervalMs = 3600000UL;

  void readAndStore();
  void processBluetooth();
  void processCommand(const String& command);
  void sendLog();
  void clearLog();
  unsigned long currentTimestamp() const;

  BluetoothSerial bluetooth_;
  SensorReading log_[kMaxLogEntries] = {};
  SensorReading latest_ = {};
  size_t logStart_ = 0;
  size_t logCount_ = 0;
  uint8_t soilPin_ = 0;
  unsigned long lastMeasure_ = 0;
  unsigned long epochOffset_ = 0;
  bool timeSynchronized_ = false;
  const char* bluetoothName_ = "";
  String commandBuffer_;
};
