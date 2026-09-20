#include "SensorLogger.h"

#include <Adafruit_AHTX0.h>
#include <BH1750.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <time.h>

namespace {
constexpr char kNusServiceUuid[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kNusRxUuid[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kNusTxUuid[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

Adafruit_AHTX0 aht20;
BH1750 bh1750;
bool aht20Ready = false;
bool bh1750Ready = false;
}

class SensorLoggerCallbacks : public BLECharacteristicCallbacks {
public:
  explicit SensorLoggerCallbacks(SensorLogger* logger) : logger_(logger) {}

  void onWrite(BLECharacteristic* characteristic) override {
    logger_->receiveBluetoothData(characteristic->getValue().c_str());
  }

private:
  SensorLogger* logger_;
};

class SensorLoggerServerCallbacks : public BLEServerCallbacks {
public:
  void onConnect(BLEServer*) override {}

  void onDisconnect(BLEServer* server) override {
    server->startAdvertising();
  }
};

void SensorLogger::begin(uint8_t soilPin) {
  soilPin_ = soilPin;
  pinMode(soilPin_, INPUT);
  analogReadResolution(12);

  aht20Ready = aht20.begin();
  bh1750Ready = bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  uint64_t mac = ESP.getEfuseMac();
  char name[14];
  snprintf(name, sizeof(name), "EnvLog-%05llX", mac & 0xFFFFF);
  bluetoothName_ = name;
  BLEDevice::init(bluetoothName_.c_str());

  server_ = BLEDevice::createServer();
  server_->setCallbacks(new SensorLoggerServerCallbacks());
  BLEService* service = server_->createService(kNusServiceUuid);
  BLECharacteristic* rxCharacteristic = service->createCharacteristic(
    kNusRxUuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxCharacteristic->setCallbacks(new SensorLoggerCallbacks(this));
  txCharacteristic_ = service->createCharacteristic(
    kNusTxUuid, BLECharacteristic::PROPERTY_NOTIFY);
  txCharacteristic_->addDescriptor(new BLE2902());
  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(kNusServiceUuid);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

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
  while (commandBuffer_.indexOf('\n') >= 0 || commandBuffer_.indexOf('\r') >= 0) {
    int lineEnd = commandBuffer_.indexOf('\n');
    int carriageReturn = commandBuffer_.indexOf('\r');
    if (lineEnd < 0 || (carriageReturn >= 0 && carriageReturn < lineEnd)) {
      lineEnd = carriageReturn;
    }
    String command = commandBuffer_.substring(0, lineEnd);
    commandBuffer_.remove(0, lineEnd + 1);
    if (command.length() != 0) {
      processCommand(command);
    }
  }
}

void SensorLogger::receiveBluetoothData(const String& data) {
  for (size_t index = 0; index < data.length(); ++index) {
    char character = data[index];
    if (character == '\n' || character == '\r') {
      commandBuffer_ += character;
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
    txCharacteristic_->setValue("OK_CLEARED\n");
    txCharacteristic_->notify();
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
      txCharacteristic_->setValue("OK_TIME\n");
      txCharacteristic_->notify();
    } else {
      txCharacteristic_->setValue("ERROR_TIME\n");
      txCharacteristic_->notify();
    }
  }
}

void SensorLogger::sendLog() {
  sendNotification("timestamp,temp,humid,light,soil,device\n");
  for (size_t index = 0; index < logCount_; ++index) {
    size_t logIndex = (logStart_ + index) % kMaxLogEntries;
    const SensorReading& reading = log_[logIndex];
    char line[128];
    snprintf(line, sizeof(line), "%lu,%.2f,%.2f,%.2f,%u,%s\n",
      reading.timestamp,
      reading.temperature,
      reading.humidity,
      reading.illuminance,
      reading.soilMoisture,
      bluetoothName_.c_str());
    sendNotification(line);
    delay(20);
  }
  sendNotification("END_LOG\n");
}

void SensorLogger::sendNotification(const char* data) {
  constexpr size_t kNotificationSize = 20;
  uint8_t notification[kNotificationSize];
  size_t length = strlen(data);
  for (size_t offset = 0; offset < length; offset += kNotificationSize) {
    size_t chunkSize = min(kNotificationSize, length - offset);
    memcpy(notification, data + offset, chunkSize);
    txCharacteristic_->setValue(notification, chunkSize);
    txCharacteristic_->notify();
    delay(10);
  }
}

void SensorLogger::clearLog() {
  logStart_ = 0;
  logCount_ = 0;
}

unsigned long SensorLogger::currentTimestamp() const {
  unsigned long elapsedSeconds = millis() / 1000UL;
  return timeSynchronized_ ? epochOffset_ + elapsedSeconds : elapsedSeconds;
}
