#include "SensorLogger.h"

#include <Adafruit_AHTX0.h>
#include <BH1750.h>
#include <time.h>

namespace {
Adafruit_AHTX0 aht20;
BH1750 bh1750;
bool aht20Ready = false;
bool bh1750Ready = false;
}

void SensorLogger::begin(uint8_t soilPin, const char* bluetoothName) {
  soilPin_ = soilPin;
  bluetoothName_ = bluetoothName;
  pinMode(soilPin_, INPUT);
  analogReadResolution(12);

  aht20Ready = aht20.begin();
  bh1750Ready = bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  bluetooth_.begin(bluetoothName);
  readAndStore();
  lastMeasure_ = millis();
}

bool SensorLogger::update() {
  processBluetooth();

  unsigned long now = millis();
  if (now - lastMeasure_ >= kMeasureIntervalMs) {
    lastMeasure_ = now;
    readAndStore();
    return true;
  }
  return false;
}

const SensorReading& SensorLogger::latest() const {
  return latest_;
}

bool SensorLogger::hasReading() const {
  return logCount_ != 0;
}

void SensorLogger::readAndStore() {
  sensors_event_t humidityEvent;
  sensors_event_t temperatureEvent;
  SensorReading reading = {};

  if (aht20Ready) {
    aht20.getEvent(&humidityEvent, &temperatureEvent);
    reading.temperature = temperatureEvent.temperature;
    reading.humidity = humidityEvent.relative_humidity;
  }
  if (bh1750Ready) {
    reading.illuminance = bh1750.readLightLevel();
  }
  reading.soilMoisture = analogRead(soilPin_);
  reading.timestamp = currentTimestamp();
  latest_ = reading;

  size_t writeIndex = (logStart_ + logCount_) % kMaxLogEntries;
  if (logCount_ == kMaxLogEntries) {
    log_[logStart_] = reading;
    logStart_ = (logStart_ + 1) % kMaxLogEntries;
  } else {
    log_[writeIndex] = reading;
    ++logCount_;
  }
}

void SensorLogger::processBluetooth() {
  while (bluetooth_.available()) {
    char character = static_cast<char>(bluetooth_.read());
    if (character == '\n' || character == '\r') {
      if (commandBuffer_.length() != 0) {
        processCommand(commandBuffer_);
        commandBuffer_ = "";
      }
    } else if (commandBuffer_.length() < 64) {
      commandBuffer_ += character;
    }
  }
}

void SensorLogger::processCommand(const String& command) {
  if (command == "GET_LOG") {
    sendLog();
  } else if (command == "CLEAR_LOG") {
    clearLog();
    bluetooth_.println("OK_CLEARED");
  } else if (command.startsWith("TIME:")) {
    unsigned long epoch = command.substring(5).toInt();
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    if (sscanf(command.c_str(), "TIME:[%u,%u,%u,%u,%u,%u]",
        &year, &month, &day, &hour, &minute, &second) == 6) {
      struct tm date = {};
      date.tm_year = year - 1900;
      date.tm_mon = month - 1;
      date.tm_mday = day;
      date.tm_hour = hour;
      date.tm_min = minute;
      date.tm_sec = second;
      epoch = static_cast<unsigned long>(mktime(&date));
    }
    if (epoch != 0) {
      epochOffset_ = epoch - millis() / 1000UL;
      timeSynchronized_ = true;
      bluetooth_.println("OK_TIME");
    } else {
      bluetooth_.println("ERROR_TIME");
    }
  }
}

void SensorLogger::sendLog() {
  bluetooth_.println("timestamp,temp,humid,light,soil,device");
  for (size_t index = 0; index < logCount_; ++index) {
    size_t logIndex = (logStart_ + index) % kMaxLogEntries;
    const SensorReading& reading = log_[logIndex];
    bluetooth_.printf("%lu,%.2f,%.2f,%.2f,%u,%s\n",
      reading.timestamp,
      reading.temperature,
      reading.humidity,
      reading.illuminance,
      reading.soilMoisture,
      bluetoothName_);
    delay(20);
  }
  bluetooth_.println("END_LOG");
}

void SensorLogger::clearLog() {
  logStart_ = 0;
  logCount_ = 0;
}

unsigned long SensorLogger::currentTimestamp() const {
  unsigned long elapsedSeconds = millis() / 1000UL;
  return timeSynchronized_ ? epochOffset_ + elapsedSeconds : elapsedSeconds;
}
