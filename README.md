# BLE Medical Gateway

ATOM Lite (ESP32) を使用して、ニプロ製BLE医療機器からデータを取得し、シリアル通信でPCに送信するゲートウェイ。

## 対応機器

| 種類 | 機器 |
|------|------|
| マイコン | M5Stack ATOM Lite (ESP32-PICO-D4) |
| 体温計 | ニプロ非接触体温計 NT-100B |
| 血圧計 | ニプロ電子血圧計 NBP-1BLE |

## ファームウェア書き込み

### Web経由（推奨）

ブラウザからワンクリックで書き込み可能:

**https://yhonda-ohishi-pub-dev.github.io/ble-medical-gateway/**

対応ブラウザ: Chrome, Edge, Opera

### PlatformIO

```bash
# ビルド
pio run

# アップロード
pio run --target upload

# シリアルモニタ
pio device monitor --baud 115200
```

## シリアル出力（JSON形式）

### 体温データ
```json
{"type":"temperature","value":36.5,"unit":"celsius"}
```

### 血圧データ
```json
{"type":"blood_pressure","systolic":120,"diastolic":80,"pulse":72,"unit":"mmHg"}
```

## LED表示

| 色 | 状態 |
|----|------|
| 青 | BLEスキャン中 |
| 緑 | 機器と接続済み |
| 白 | データ受信 |
| 赤 | エラー |

## ボタン操作

- ボタン押下: BLEスキャン再開、接続リセット

## デバッグモード

`src/main.cpp` の `debugMode` を `true` に設定すると、スキャン結果や接続状態などの詳細ログが出力されます。

```cpp
bool debugMode = true;  // デバッグ出力ON
```

## 使用方法

1. ATOM LiteにファームウェアをWeb経由または PlatformIO で書き込み
2. 体温計/血圧計で測定
3. シリアルポート(115200bps)でJSONデータを受信

## ライセンス

MIT License
