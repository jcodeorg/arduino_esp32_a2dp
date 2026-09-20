// A2DPは、スマートフォンから音楽を受信するBluetoothの仕組みです。
#include "BluetoothA2DPSink.h"
#include <Wire.h>
#include <U8g2lib.h>
#include "NeoPixelController.h"
#include "SensorLogger.h"

// スマートフォンのBluetooth一覧に表示されるスピーカー名です。
const char kA2dpDeviceName[] = "BT_Speaker5.5";

// I2Cで接続した128x64ドットのOLED画面です。
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// このプログラムで使う機能の担当者を作ります。
BluetoothA2DPSink a2dp_sink;
NeoPixelController neoPixelController;
SensorLogger sensorLogger;

// 音楽情報と画面更新に使う変数です。
String currentTitle = "未接続";
String currentArtist = "";
int currentVolumePercent = 0; // 音量（0〜100%）
bool bluetoothConnected = false;
bool musicPlaying = false;
unsigned long lastDisplayUpdate = 0;

// OLEDの内容を現在の状態に合わせて描き直します。
void updateDisplay() {
  u8g2.clearBuffer();

  // 音楽を再生していないときは、センサーの情報を表示します。
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
  
  // 音楽を再生中は、音量と曲名を表示します。
  // 上の部分は小さい英数字フォントで音量を表示します。
  u8g2.setFont(u8g2_font_6x10_tf);
  
  // 例: 「Vol: 80%」という文字を作って表示します。
  char volStr[16];
  snprintf(volStr, sizeof(volStr), "Vol: %3d%%", currentVolumePercent);
  u8g2.drawStr(0, 10, volStr);

  // 音量の割合を、0〜50ドットの長さに変換して棒グラフにします。
  int barWidth = map(currentVolumePercent, 0, 100, 0, 50);
  u8g2.drawFrame(75, 2, 52, 9); // 棒グラフの外枠
  u8g2.drawBox(76, 3, barWidth, 7); // 音量に応じた中身

  u8g2.drawLine(0, 13, 128, 13);            // 区切り線

  // 下の部分は、日本語を含む曲名とアーティスト名を表示します。
  u8g2.setFont(u8g2_font_unifont_t_japanese1);

  // 曲名を表示します。drawUTF8は日本語も扱える関数です。
  u8g2.drawUTF8(0, 32, currentTitle.c_str());

  // アーティスト名が空でないときだけ表示します。
  if (currentArtist.length() > 0) {
    u8g2.drawUTF8(0, 54, currentArtist.c_str());
  }

  // ここまでの描画内容を、実際のOLED画面へ送ります。
  u8g2.sendBuffer();
}

// スマートフォンから曲名やアーティスト名を受け取ったときに呼ばれます。
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

// スマートフォン側で音量が変わったときに呼ばれます。
void volume_changed_callback(int volume) {
  currentVolumePercent = map(volume, 0, 127, 0, 100);
  if (bluetoothConnected && musicPlaying) {
    updateDisplay();
  }
}

void audio_state_changed_callback(esp_a2d_audio_state_t state, void *) {
  // 音声が流れ始めたかどうかを保存し、LEDと画面を更新します。
  musicPlaying = state == ESP_A2D_AUDIO_STATE_STARTED;
  neoPixelController.setPlaying(musicPlaying);
  updateDisplay();
}

void connection_state_changed_callback(esp_a2d_connection_state_t state, void *) {
  // スマートフォンとの接続状態を保存します。
  bluetoothConnected = state == ESP_A2D_CONNECTION_STATE_CONNECTED;
  if (!bluetoothConnected) {
    musicPlaying = false;
  }
  updateDisplay();
}

void setup() {
  // setupは、電源を入れた直後に1回だけ実行されます。
  Serial.begin(115200);

  neoPixelController.begin(18);

  // I2Cの配線を設定します。Wire.begin(SDA, SCL)の順です。
  Wire.begin(21, 22);
  
  // OLEDを使える状態にします。
  u8g2.begin();
  u8g2.enableUTF8Print(); // UTF-8（日本語）描画を有効化

  sensorLogger.begin(32);
  updateDisplay();
  lastDisplayUpdate = millis();

  // I2Sは、Bluetooth音声をアンプへ送るための通信方式です。
  // BCK=27、WS=25、音声データ出力=DOUT=26に配線しています。
  i2s_pin_config_t my_pin_config = {
    .bck_io_num = 27,
    .ws_io_num = 25,
    .data_out_num = 26,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  a2dp_sink.set_pin_config(my_pin_config);

  // Bluetoothでイベントが起きたときの通知先を登録します。
  a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
  a2dp_sink.set_on_volumechange(volume_changed_callback);
  a2dp_sink.set_on_audio_state_changed(audio_state_changed_callback);
  a2dp_sink.set_on_connection_state_changed(connection_state_changed_callback);

  // Bluetoothスピーカーとして起動します。
  a2dp_sink.start(kA2dpDeviceName);
}

void loop() {
  // loopは何度も繰り返し実行されます。
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