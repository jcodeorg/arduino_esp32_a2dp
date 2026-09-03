#include "BluetoothA2DPSink.h"
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>

// WS2812B設定
constexpr uint8_t WS2812B_PIN = 18;
constexpr uint16_t WS2812B_COUNT = 1;
Adafruit_NeoPixel ws2812b(WS2812B_COUNT, WS2812B_PIN, NEO_GRB + NEO_KHZ800);
const uint32_t ws2812bColors[] = {
  0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00,
  0x0000FF, 0x4B0082, 0x8F00FF
};
constexpr uint8_t WS2812B_COLOR_COUNT = sizeof(ws2812bColors) / sizeof(ws2812bColors[0]);
constexpr unsigned long WS2812B_INTERVAL_MS = 30;
constexpr unsigned long WS2812B_TRANSITION_MS = 3000;
bool isPlaying = false;
uint8_t ws2812bColorIndex = 0;
unsigned long lastWs2812bUpdate = 0;
unsigned long ws2812bTransitionStart = 0;

// I2C接続のOLED設定 (SSD1306 128x64)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

BluetoothA2DPSink a2dp_sink;

// 表示用変数
String currentTitle = "未接続";
String currentArtist = "";
int currentVolumePercent = 0; // 音量 (0 ~ 100%)

// 画面全体を再描画する関数
void updateDisplay() {
  u8g2.clearBuffer();
  
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

  if (updated) {
    updateDisplay();
  }
}

// 音量変更時のコールバック関数
void volume_changed_callback(int volume) {
  currentVolumePercent = map(volume, 0, 127, 0, 100);
  updateDisplay();
}

void audio_state_changed_callback(esp_a2d_audio_state_t state, void *) {
  isPlaying = (state == ESP_A2D_AUDIO_STATE_STARTED);

  if (isPlaying) {
    ws2812bTransitionStart = millis();
    lastWs2812bUpdate = ws2812bTransitionStart;
  }

  if (!isPlaying) {
    ws2812bColorIndex = 3;
    ws2812b.setPixelColor(0, ws2812bColors[ws2812bColorIndex]);
    ws2812b.show();
  }
}

void setup() {
  Serial.begin(115200);

  // WS2812Bを緑で点灯
  ws2812b.begin();
  ws2812b.setBrightness(255);
  ws2812b.setPixelColor(0, ws2812bColors[3]);
  ws2812b.show();

  // SDAをGPIO 19、SCLをGPIO 32 に設定
  // （配線に合わせて Wire.begin(SDA_PIN, SCL_PIN) の順で指定します）
  Wire.begin(21, 22);
  
  // OLED初期化
  u8g2.begin();
  u8g2.enableUTF8Print(); // UTF-8（日本語）描画を有効化

  // 初期画面
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_unifont_t_japanese1);
  u8g2.drawUTF8(0, 20, "BTスピーカー5.3");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 40, "Ready...");
  u8g2.sendBuffer();

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

  // Bluetooth起動
  a2dp_sink.start("BT_Speaker5.3");
}

void loop() {
  if (isPlaying && millis() - lastWs2812bUpdate >= WS2812B_INTERVAL_MS) {
    unsigned long now = millis();
    lastWs2812bUpdate = now;
    unsigned long transitionElapsed = now - ws2812bTransitionStart;
    if (transitionElapsed >= WS2812B_TRANSITION_MS) {
      ws2812bColorIndex = (ws2812bColorIndex + transitionElapsed / WS2812B_TRANSITION_MS)
        % WS2812B_COLOR_COUNT;
      ws2812bTransitionStart = now;
      transitionElapsed = 0;
    }
    uint8_t nextColorIndex = (ws2812bColorIndex + 1) % WS2812B_COLOR_COUNT;
    uint32_t currentColor = ws2812bColors[ws2812bColorIndex];
    uint32_t nextColor = ws2812bColors[nextColorIndex];
    uint8_t red = ((currentColor >> 16) * (WS2812B_TRANSITION_MS - transitionElapsed)
      + (nextColor >> 16) * transitionElapsed) / WS2812B_TRANSITION_MS;
    uint8_t green = (((currentColor >> 8) & 0xFF) * (WS2812B_TRANSITION_MS - transitionElapsed)
      + ((nextColor >> 8) & 0xFF) * transitionElapsed) / WS2812B_TRANSITION_MS;
    uint8_t blue = ((currentColor & 0xFF) * (WS2812B_TRANSITION_MS - transitionElapsed)
      + (nextColor & 0xFF) * transitionElapsed) / WS2812B_TRANSITION_MS;
    ws2812b.setPixelColor(0, red, green, blue);
    ws2812b.show();

  }
}