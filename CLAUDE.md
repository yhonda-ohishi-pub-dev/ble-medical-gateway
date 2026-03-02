# BLE Medical Gateway Project

## 概要
ATOM Lite (ESP32) を使用して、ニプロ製BLE体温計・血圧計からデータを取得し、Windows PCにシリアル通信で送信するゲートウェイ。

## 使用機器
- **マイコン**: M5Stack ATOM Lite (ESP32-PICO-D4)
- **体温計**: ニプロ非接触体温計 NT-100B (BLE)
- **血圧計**: ニプロ電子血圧計 NBP-1BLE (BLE)

## 開発環境
- VSCode + PlatformIO
- ボード: `m5stack-atom`
- フレームワーク: Arduino

## PlatformIO コマンド

```bash
# プロジェクトフォルダに移動
cd c:\arduino\ble-medical-gateway

# ビルド
~/.platformio/penv/Scripts/pio.exe run

# アップロード (ATOM Liteに書き込み)
~/.platformio/penv/Scripts/pio.exe run --target upload

# シリアルモニタ (115200bps)
~/.platformio/penv/Scripts/pio.exe device monitor --baud 115200

# クリーンビルド
~/.platformio/penv/Scripts/pio.exe run --target clean

# 接続デバイス一覧
~/.platformio/penv/Scripts/pio.exe device list
```

## シリアル出力 (JSON形式)

### スキャン結果
```json
{"type":"scan","name":"デバイス名","address":"aa:bb:cc:dd:ee:ff","rssi":-50}
```

### 体温データ
```json
{"type":"temperature","value":36.5,"unit":"celsius"}
```

### 血圧データ
```json
{"type":"blood_pressure","systolic":120,"diastolic":80,"pulse":72,"unit":"mmHg"}
```

### ステータス
```json
{"type":"found","device":"thermometer"}
{"type":"connected","device":"blood_pressure"}
{"type":"disconnected","device":"thermometer"}
{"type":"error","message":"エラー内容"}
```

## LED表示
- 青: BLEスキャン中
- 緑: 接続済み
- 白: データ受信
- 赤: エラー

## ボタン操作
- ボタン押下: BLEスキャン再開、接続リセット

## BLE UUID
- Health Thermometer Service: `0x1809`
- Blood Pressure Service: `0x1810`
- Temperature Measurement: `0x2A1C`
- Blood Pressure Measurement: `0x2A35`

## 注意事項
- ニプロ機器は測定時にBLEアドバタイズを開始する
- ペアリングが必要な場合あり
- 標準プロファイルに対応しない場合は機器固有UUIDの調査が必要
