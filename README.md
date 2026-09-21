# arduino_esp32_a2dp

ESP32-WROOM-32E を使用した、Arduino版 A2DP Bluetoothスピーカーのファームウェアリポジトリです。
GitHub Actions を利用して、ブラウザ上だけでファームウェアの書き換えとビルドが行えます。

ESP32-WROOM-32E 用の Bluetooth スピーカー主な機能：

- NeoPixcel：デバイスの動作モードに合わせて発光色を変化
- 音楽再生時: 楽曲名、歌手名、音量を表示
- 音楽停止時: 各種センサー値（温度・湿度・照度・土壌水分）を表示
- 環境データ出力: 1時間ごとにデータを蓄積し、Webブラウザから Bluetooth 経由でデータをダウンロード（CSV形式で保存可能）

## 手順

### 1. リポジトリのフォーク
1. GitHub アカウントを作成してサインインします。
2. [jcodeorg/arduino_esp32_a2dp](https://github.com/jcodeorg/arduino_esp32_a2dp) を開き、右上の **【Fork】** ボタンを押してご自身のアカウントにコピーを作成します。

### 2. ファームウェア（設定）の編集
1. フォークしたリポジトリで `/firmware/firmware.ino` を開きます。
2. 編集（鉛筆アイコン）ボタンを押し、以下の箇所の文字列を好みに合わせて変更します：
   - **OLED初期表示（91行目付近）：** `"BTスピーカー5.2"`
   - **Bluetoothデバイス名（111行目付近）：** `"BT_Speaker5.2"`
3. 編集後、右上の **【Commit Changes...】** をクリックして保存します。

### 3. GitHub Actions の有効化と実行
1. 上部タブメニューから **【Actions】** を選択します。
2. 初回のみ表示される **【I understand my workflows, go ahead and enable them】** をクリックします。
3. 上部タブメニューの **【Code】** に戻り、`README.md` などを適当に編集して **【Commit Changes...】** を行うと、ビルドが自動開始されます。

### 4. ビルドファイルのダウンロード
1. 上部タブメニューの **【Actions】** を選択します。
2. 進行中のビルドを選択します（約1分で完了します）。
3. ビルド完了後、ページ下部の **Artifacts** にあるダウンロードアイコンをクリックし、`esp32-a2dp-firmware.zip` を取得します。

---

### 解凍後の確認
ダウンロードした ZIP ファイルを解凍し、以下の3つのファイルが含まれているか確認してください：

* `bootloader.bin`
* `partitions.bin`
* `firmware.bin`

---

## デバイスへの書き込み手順

1. ESP32 デバイスを PC に USB ケーブルで接続します。
2. ブラウザで [ESP Flash Tool (esptool-js)](https://espressif.github.io/esptool-js/) を開きます。
3. **【Connect】** ボタンをクリックし、接続された ESP32 のシリアルポートを選択して接続します。
4. **【Add File】** ボタンを使い、以下のアドレス（オフセット）とファイルの組み合わせで 3 つのファイルをセットします：

   | アドレス | ファイル名 |
   | :--- | :--- |
   | **`0x1000`** | `bootloader.bin` |
   | **`0x8000`** | `partitions.bin` |
   | **`0x10000`** | `firmware.bin` |

5. 設定項目（Flash Mode / Flash Frequency / Flash Size）は **「Keep」** のままで問題ありません。
6. **【Program】** ボタンをクリックして書き込みを開始します。
7. 画面に以下のメッセージが表示されれば書き込み完了です：

   ```text
   Hash of data verified.
   Leaving...
   Hard resetting via RTS pin...


---

## 動作確認

デバイスのリセットボタンを押すか、USB を抜き差しして再起動し、以下を確認してください：

+ OLED ディスプレイに初期文字列が表示されること
+ スマホ等の Bluetooth 設定からペアリングし、音声が正常に再生できること