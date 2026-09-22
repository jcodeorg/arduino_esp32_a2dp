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

// UTF-8の文字列を1文字ずつ描画します。
// フォントにない文字は、フォントに頼らず四角形を直接描きます。
void drawUtf8WithTofu(uint8_t x, uint8_t y, const String& text) {
  constexpr uint8_t kTofuSize = 12;
  constexpr uint8_t kTofuAdvance = 16;
  uint8_t cursorX = x;

  for (size_t index = 0; index < text.length();) {
    const uint8_t firstByte = static_cast<uint8_t>(text[index]);
    size_t byteCount = 0;
    uint32_t codePoint = 0;

    // UTF-8の先頭バイトから、1文字のバイト数を判定します。
    if (firstByte < 0x80) {
      codePoint = firstByte;
      byteCount = 1;
    } else if ((firstByte & 0xE0) == 0xC0 && index + 1 < text.length()) {
      codePoint = firstByte & 0x1F;
      byteCount = 2;
    } else if ((firstByte & 0xF0) == 0xE0 && index + 2 < text.length()) {
      codePoint = firstByte & 0x0F;
      byteCount = 3;
    } else if ((firstByte & 0xF8) == 0xF0 && index + 3 < text.length()) {
      codePoint = firstByte & 0x07;
      byteCount = 4;
    } else {
      // 壊れたUTF-8も、表示できない文字としてトーフを描きます。
      u8g2.drawFrame(cursorX, y - kTofuSize, kTofuSize, kTofuSize);
      cursorX += kTofuAdvance;
      ++index;
      continue;
    }

    // 2バイト目以降をつなげて、Unicodeの文字コードを作ります。
    bool valid = true;
    for (size_t offset = 1; offset < byteCount; ++offset) {
      const uint8_t nextByte = static_cast<uint8_t>(text[index + offset]);
      if ((nextByte & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      codePoint = (codePoint << 6) | (nextByte & 0x3F);
    }

    if (!valid) {
      u8g2.drawFrame(cursorX, y - kTofuSize, kTofuSize, kTofuSize);
      cursorX += kTofuAdvance;
      ++index;
      continue;
    }

    // フォントに文字があれば、実際のグリフを描画します。
    // 16ビットを超える文字は、このフォントでは表示できないためトーフにします。
    if (codePoint <= 0xFFFF && u8g2_IsGlyph(u8g2.getU8g2(), codePoint)) {
      u8g2.drawGlyph(cursorX, y, static_cast<uint16_t>(codePoint));
      cursorX += static_cast<uint8_t>(u8g2_GetGlyphWidth(
        u8g2.getU8g2(), static_cast<uint16_t>(codePoint)));
    } else {
      // □の字形がフォントにない場合でも、四角形なら必ず表示できます。
      u8g2.drawFrame(cursorX, y - kTofuSize, kTofuSize, kTofuSize);
      cursorX += kTofuAdvance;
    }

    index += byteCount;
  }
}

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

  // フォントにない文字は、四角形のトーフとして表示します。
  drawUtf8WithTofu(0, 32, currentTitle);

  // アーティスト名が空でないときだけ表示します。
  if (currentArtist.length() > 0) {
    drawUtf8WithTofu(0, 54, currentArtist);
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
  // Wire.begin(21, 22);
  Wire.begin(19, 5); // SDA=19, SCL=5 開発用ボードの配線
  

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