#include "BluetoothA2DPSink.h"

BluetoothA2DPSink a2dp_sink;

void setup() {
    // I2S ピンアサインの設定 (必要に応じてご自身の基板に合わせて変更)
    i2s_pin_config_t my_pin_config = {
        .bck_io_num = 27,
        .ws_io_num = 25,
        .data_out_num = 26,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    a2dp_sink.set_pin_config(my_pin_config);
    a2dp_sink.start("BT_Speaker5");
}

void loop() {
}
