#include "BluetoothA2DPSink.h"
#include <Wire.h>
#include <U8g2lib.h>
#include "NeoPixelController.h"
#include "SensorLogger.h"

const char kA2dpDeviceName[] = "BT_Speaker5.4";

// I2C接続のOLED設定 (SSD1306 128x64)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

BluetoothA2DPSink a2dp_sink;
NeoPixelController neoPixelController;
SensorLogger sensorLogger;

// 表示用変数
String currentTitle = "未接続";
String currentArtist = "";
int currentVolumePercent = 0; // 音量 (0 ~ 100%)
bool bluetoothConnected = false;
bool musicPlaying = false;
unsigned long lastDisplayUpdate = 0;

// 画面全体を再描画する関数
void updateDisplay() {
  u8g2.clearBuffer();

  if (!bluetoothConnected || !musicPlaying) {
    u8g2.setFont(u8g2_font_6x10_tf);
    const SensorReading& reading = sensorLogger.latest();

    char sensorLine[32];
    char timeLine[24];
    if (sensorLogger.isBluetoothConnected()) {
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, 0, 128, 10);
      u8g2.setDrawColor(0);
    }
    u8g2.drawStr(0, 9, sensorLogger.bluetoothName());
    u8g2.setDrawColor(1);

    if (sensorLogger.getCurrentTime(timeLine, sizeof(timeLine))) {
      u8g2.drawStr(0, 20, timeLine);
    } else {
      u8g2.drawStr(0, 20, "RTC: --/--/-- --:--:--");
    }

    snprintf(sensorLine, sizeof(sensorLine), "Log: %u", static_cast<unsigned int>(sensorLogger.logCount()));
    u8g2.drawStr(0, 31, sensorLine);

    snprintf(sensorLine, sizeof(sensorLine), "T:%5.1fC H:%5.1f%%",
      reading.temperature, reading.humidity);
    u8g2.drawStr(0, 42, sensorLine);

    snprintf(sensorLine, sizeof(sensorLine), "Light: %6.1f lx", reading.illuminance);
    u8g2.drawStr(0, 53, sensorLine);

    snprintf(sensorLine, sizeof(sensorLine), "Soil: %4u", reading.soilMoisture);
    u8g2.drawStr(0, 64, sensorLine);
    u8g2.sendBuffer();
    return;
  }
  
  // --------------------------------------------------
  // 1. 音量表示エリア（最上部：英数フォント）
  // --------------------------------------------------
  u8g2.setFont(u8g2_font_6x10_tf);
  
  // "Vol: 80%" のテキスト描画
  char volStr[16];
  snprintf(volStr, sizeof(volStr), "Vol: %3d%%", currentVolumePercent);
  u8g2.drawStr(0, 10, volStr);

  // 音量バーの描画
  int barWidth = map(currentVolumePercent, 0, 100, 0, 50); // 50ピクセル幅に変換
  u8g2.drawFrame(75, 2, 52, 9);             // 外枠
  u8g2.drawBox(76, 3, barWidth, 7);           // バーの中身

  u8g2.drawLine(0, 13, 128, 13);            // 区切り線

  // --------------------------------------------------
  // 2. 曲名・アーティスト表示エリア（日本語フォント）
  // --------------------------------------------------
  // 日本語フォントに切り替え (美咲フォント UTF-8)
  u8g2.setFont(u8g2_font_unifont_t_japanese1); 

  // 曲名の表示 (y=32)
  u8g2.drawUTF8(0, 32, currentTitle.c_str());

  // アーティスト名の表示 (y=54)
  if (currentArtist.length() > 0) {
    // アーティスト名も日本語フォントのまま描画します
    u8g2.drawUTF8(0, 54, currentArtist.c_str());
  }

  u8g2.sendBuffer(); // 画面転送
}

// メタデータ（曲名・アーティスト名）の受信コールバック
void avrc_metadata_callback(uint8_t id, const uint8_t *text) {
  bool updated = false;

  if (id == ESP_AVRC_MD_ATTR_TITLE) {
    currentTitle = (char*)text;
    updated = true;
  } else if (id == ESP_AVRC_MD_ATTR_ARTIST) {
    currentArtist = (char*)text;
    updated = true;
  }

  if (updated && bluetoothConnected && musicPlaying) {
    updateDisplay();
  }
}

// 音量変更時のコールバック関数
void volume_changed_callback(int volume) {
  currentVolumePercent = map(volume, 0, 127, 0, 100);
  if (bluetoothConnected && musicPlaying) {
    updateDisplay();
  }
}

void audio_state_changed_callback(esp_a2d_audio_state_t state, void *) {
  musicPlaying = state == ESP_A2D_AUDIO_STATE_STARTED;
  neoPixelController.setPlaying(musicPlaying);
  updateDisplay();
}

void connection_state_changed_callback(esp_a2d_connection_state_t state, void *) {
  bluetoothConnected = state == ESP_A2D_CONNECTION_STATE_CONNECTED;
  if (!bluetoothConnected) {
    musicPlaying = false;
  }
  updateDisplay();
}

void setup() {
  Serial.begin(115200);

  neoPixelController.begin(18);

  // SDAをGPIO 19、SCLをGPIO 32 に設定
  // （配線に合わせて Wire.begin(SDA_PIN, SCL_PIN) の順で指定します）
  Wire.begin(21, 22);
  
  // OLED初期化
  u8g2.begin();
  u8g2.enableUTF8Print(); // UTF-8（日本語）描画を有効化

  sensorLogger.begin(32);
  updateDisplay();
  lastDisplayUpdate = millis();

  // I2S ピン設定 (BCK: 27, WS: 25, DOUT: 26)
  i2s_pin_config_t my_pin_config = {
    .bck_io_num = 27,
    .ws_io_num = 25,
    .data_out_num = 26,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  a2dp_sink.set_pin_config(my_pin_config);

  // コールバック登録
  a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
  a2dp_sink.set_on_volumechange(volume_changed_callback);
  a2dp_sink.set_on_audio_state_changed(audio_state_changed_callback);
  a2dp_sink.set_on_connection_state_changed(connection_state_changed_callback);

  // Bluetooth起動
  a2dp_sink.start(kA2dpDeviceName);
}

void loop() {
  neoPixelController.update();
  bool newReading = sensorLogger.update();
  unsigned long now = millis();
  if ((!bluetoothConnected || !musicPlaying)
      && (newReading || now - lastDisplayUpdate >= 1000UL)) {
    lastDisplayUpdate = now;
    sensorLogger.refreshSensors();
    updateDisplay();
  }
}