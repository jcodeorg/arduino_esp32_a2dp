#include "SensorLogger.h"

#include <Adafruit_AHTX0.h>
#include <BH1750.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <time.h>

namespace {
// BLEサービスと、その中の2本の通信路を識別する番号です。
// UUIDは、Bluetooth上で機能を区別するための長い名前です。
constexpr char kNusServiceUuid[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kNusRxUuid[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kNusTxUuid[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kBleDeviceNamePrefix[] = "EnvLog";
constexpr int64_t kInitialYear = 1700;
constexpr int64_t kSecondsPerDay = 86400;

// センサー本体と、センサーが使える状態かどうかを保存します。
Adafruit_AHTX0 aht20;
BH1750 bh1750;
bool aht20Ready = false;
bool bh1750Ready = false;

bool isLeapYear(int year) {
  // 4年ごと。ただし100年ごとは除き、400年ごとはうるう年です。
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}
}

// BLEの受信側の処理です。アプリがデータを書き込むと呼ばれます。
class SensorLoggerCallbacks : public BLECharacteristicCallbacks {
public:
  explicit SensorLoggerCallbacks(SensorLogger* logger) : logger_(logger) {}

  void onWrite(BLECharacteristic* characteristic) override {
    // 受信した文字をSensorLoggerへ渡します。
    logger_->receiveBluetoothData(characteristic->getValue().c_str());
  }

private:
  SensorLogger* logger_;
};

// BLEの接続と切断を知らせる処理です。
class SensorLoggerServerCallbacks : public BLEServerCallbacks {
public:
  explicit SensorLoggerServerCallbacks(SensorLogger* logger) : logger_(logger) {}

  void onConnect(BLEServer*) override {
    // 接続中は、画面に接続中であることを表示できます。
    logger_->bluetoothConnected_ = true;
  }

  void onDisconnect(BLEServer* server) override {
    // 切断されたら、次の接続を待つために広告を再開します。
    logger_->bluetoothConnected_ = false;
    logger_->commandBuffer_ = "";
    logger_->lastAdvertise_ = millis();
    server->startAdvertising();
  }

private:
  SensorLogger* logger_;
};

void SensorLogger::begin(uint8_t soilPin) {
  // センサーとBLE通信を使い始めるための初期設定です。
  soilPin_ = soilPin;
  pinMode(soilPin_, INPUT);
  analogReadResolution(12);

  // センサーの初期化に成功したかを保存します。
  aht20Ready = aht20.begin();
  bh1750Ready = bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  // ESP32固有の番号を名前に加え、同じ名前の機器と区別します。
  uint64_t mac = ESP.getEfuseMac();
  char name[14];
  snprintf(name, sizeof(name), "%s-%05llX", kBleDeviceNamePrefix, mac & 0xFFFFF);
  bluetoothName_ = name;
  BLEDevice::init(bluetoothName_.c_str());

  // BLEサーバーと、センサー通信用のサービスを作ります。
  server_ = BLEDevice::createServer();
  server_->setCallbacks(new SensorLoggerServerCallbacks(this));
  BLEService* service = server_->createService(kNusServiceUuid);
  // RXはアプリからESP32へ命令を送る通路です。
  BLECharacteristic* rxCharacteristic = service->createCharacteristic(
    kNusRxUuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxCharacteristic->setCallbacks(new SensorLoggerCallbacks(this));
  // TXはESP32からアプリへログを送る通路です。
  txCharacteristic_ = service->createCharacteristic(
    kNusTxUuid, BLECharacteristic::PROPERTY_NOTIFY);
  txCharacteristic_->addDescriptor(new BLE2902());
  service->start();

  // BLE広告は「この機器が近くにいます」と周囲へ知らせる仕組みです。
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(kNusServiceUuid);
  advertising->setScanResponse(true);
  // A2DP開始後に共通のBluetooth名が変わっても、BLE名を返せるようにします。
  BLEAdvertisementData scanResponseData;
  scanResponseData.setName(bluetoothName_.c_str());
  advertising->setScanResponseData(scanResponseData);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  // 起動直後にも1回計測して、画面に値を表示できるようにします。
  readAndStore();
  lastMeasure_ = millis();
  lastAdvertise_ = millis();
}

bool SensorLogger::update() {
  // loopから繰り返し呼ばれ、通信と定期計測を担当します。
  processBluetooth();

  unsigned long now = millis();
  if (!bluetoothConnected_ && now - lastAdvertise_ >= kAdvertiseRetryIntervalMs) {
    // 接続されていなければ、定期的にBLE広告を出し直します。
    lastAdvertise_ = now;
    BLEDevice::startAdvertising();
  }

  if (now - lastMeasure_ >= kMeasureIntervalMs) {
    // 計測間隔を過ぎたら、新しいログを保存します。
    lastMeasure_ = now;
    readAndStore();
    return true;
  }
  return false;
}

void SensorLogger::refreshSensors() {
  // センサーを読み直します。ログには保存せず、画面表示だけを更新します。
  SensorReading reading = {};
  sensors_event_t humidityEvent;
  sensors_event_t temperatureEvent;

  if (aht20Ready) {
    aht20.getEvent(&humidityEvent, &temperatureEvent);
    reading.temperature = temperatureEvent.temperature;
    reading.humidity = humidityEvent.relative_humidity;
  }
  if (bh1750Ready) {
    reading.illuminance = bh1750.readLightLevel();
  }
  reading.soilMoisture = analogRead(soilPin_);
  reading.timestamp = latest_.timestamp;
  latest_ = reading;
}

const SensorReading& SensorLogger::latest() const {
  return latest_;
}

bool SensorLogger::hasReading() const {
  return logCount_ != 0;
}

const char* SensorLogger::bluetoothName() const {
  return bluetoothName_.c_str();
}

bool SensorLogger::isBluetoothConnected() const {
  return bluetoothConnected_;
}

size_t SensorLogger::logCount() const {
  return logCount_;
}

bool SensorLogger::formatTimestamp(int64_t timestamp, char* buffer, size_t bufferSize) const {
  // 秒数を、人が読みやすい「年/月/日 時:分:秒」に変換します。
  if (bufferSize == 0) {
    return false;
  }

  if (timeSynchronized_) {
    // 時刻合わせ済みなら、ESP32の標準時刻変換を使います。
    time_t unixTimestamp = static_cast<time_t>(timestamp);
    struct tm date;
    localtime_r(&unixTimestamp, &date);
    strftime(buffer, bufferSize, "%Y/%m/%d %H:%M:%S", &date);
    return true;
  }

  // 時刻合わせ前は、起動してからの経過秒数を仮の日付として表示します。
  int year = static_cast<int>(kInitialYear);
  int month = 1;
  int day = 1;
  int64_t days = timestamp / kSecondsPerDay;
  int seconds = static_cast<int>(timestamp % kSecondsPerDay);
  while (days >= (isLeapYear(year) ? 366 : 365)) {
    days -= isLeapYear(year) ? 366 : 365;
    ++year;
  }
  const int daysInMonth[] = {
    31, isLeapYear(year) ? 29 : 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30, 31
  };
  while (days >= daysInMonth[month - 1]) {
    days -= daysInMonth[month - 1];
    ++month;
  }
  day += static_cast<int>(days);
  snprintf(buffer, bufferSize, "%04d/%02d/%02d %02d:%02d:%02d",
    year, month, day, seconds / 3600, (seconds / 60) % 60, seconds % 60);
  return true;
}

bool SensorLogger::getCurrentTime(char* buffer, size_t bufferSize) const {
  return formatTimestamp(currentTimestamp(), buffer, bufferSize);
}

void SensorLogger::readAndStore() {
  // センサーを読み取り、最新値とログの両方へ保存します。
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

  // 配列が満杯になったら、一番古いデータから上書きします。
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
  // 受信した文字を改行ごとの命令に分けて実行します。
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
  // BLEで届いた文字を命令バッファへ追加します。
  for (size_t index = 0; index < data.length(); ++index) {
    char character = data[index];
    if (character == '\n' || character == '\r') {
      commandBuffer_ += character;
    } else if (commandBuffer_.length() < 64) {
      commandBuffer_ += character;
    }
  }
}

void SensorLogger::applyHistoricalTimeAdjustment(int64_t epoch) {
  // 時刻合わせ前に保存したログへ、正しい時刻との差を加えます。
  if (historicalAdjustmentApplied_ || timeSynchronized_) {
    return;
  }

  const int64_t elapsedSeconds = millis() / 1000UL;
  historicalEpochOffset_ = epoch - elapsedSeconds;
  for (size_t index = 0; index < logCount_; ++index) {
    size_t logIndex = (logStart_ + index) % kMaxLogEntries;
    const int64_t storedSeconds = log_[logIndex].timestamp;
    if (storedSeconds <= elapsedSeconds) {
      log_[logIndex].timestamp = storedSeconds + historicalEpochOffset_;
    }
  }
  historicalAdjustmentApplied_ = true;
}

void SensorLogger::processCommand(const String& command) {
  // アプリから届いた命令を判定します。
  if (command == "GET_LOG") {
    // 保存済みのログを順番に送ります。
    sendLog();
  } else if (command == "CLEAR_LOG") {
    // ログを空にして、完了通知を送ります。
    clearLog();
    txCharacteristic_->setValue("OK_CLEARED\n");
    txCharacteristic_->notify();
  } else if (command.startsWith("TIME:")) {
    // アプリから受け取った時刻で、時計を合わせます。
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
      applyHistoricalTimeAdjustment(static_cast<int64_t>(epoch));
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
  // 保存したログを1行ずつ作り、BLE通知で送ります。
  for (size_t index = 0; index < logCount_; ++index) {
    size_t logIndex = (logStart_ + index) % kMaxLogEntries;
    const SensorReading& reading = log_[logIndex];
    char timestamp[32];
    char line[160];
    const bool timestampValid = formatTimestamp(reading.timestamp, timestamp, sizeof(timestamp));
    if (!timestampValid) {
      timestamp[0] = '\0';
    }
    snprintf(line, sizeof(line), "%s,%.2f,%.2f,%u,%.2f,%s\n",
      timestamp,
      reading.temperature,
      reading.humidity,
      reading.soilMoisture,
      reading.illuminance,
      bluetoothName_.c_str());
    sendNotification(line);
    delay(20);
  }
  sendNotification("END_LOG\n");
}

void SensorLogger::sendNotification(const char* data) {
  // BLE通知には一度に送れる長さの制限があるため、短く分割します。
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
  // 配列の中身を消す代わりに、件数を0にして空として扱います。
  logStart_ = 0;
  logCount_ = 0;
}

int64_t SensorLogger::currentTimestamp() const {
  // 現在の経過秒数に時刻の差を足して、現在時刻を返します。
  int64_t elapsedSeconds = millis() / 1000UL;
  return timeSynchronized_ ? epochOffset_ + elapsedSeconds : elapsedSeconds;
}
