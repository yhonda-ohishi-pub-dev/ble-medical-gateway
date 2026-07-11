# firmware-rust (PoC)

ATOM Lite (ESP32-PICO-D4, Xtensa) 版ファームウェアを Rust (`esp-idf-hal`/`esp-idf-svc` +
`esp32-nimble`) で書けるかの実現性検証 PoC。Refs #1

`../src/main.cpp` (Arduino/NimBLE-Arduino 版、本番) を置き換えるものではない。
BLE scan → central 接続 → notify 購読までの骨組みのみで、体温/血圧データのデコード・
シリアル JSON 出力・LED/ボタン制御は含まない (フル移植は別スコープ)。

## このリポジトリでの検証状況

- ソースコード (`src/main.rs`) は `esp32-nimble` 0.12.0 / `esp-idf-svc` 0.52 の実際の
  API (`taks/esp32-nimble` の `examples/ble_scan.rs` / `examples/ble_client.rs` を参照)
  に基づいて書いた。`rustc` での構文チェックは通過している (依存クレート unresolved の
  エラーのみ、構文エラーなし)。
- **`cargo build` による実ビルド検証はできていない。** このタスクを実行した CCoW
  リモートコンテナは GitHub への outbound アクセスがセッションにアタッチされた repo
  のみに制限されており (`api.github.com` への任意 repo アクセスが 403)、Xtensa 用
  Rust toolchain (`espup install`) が取得する `esp-rs/rust-build` の GitHub Releases
  をダウンロードできなかった。そのためこの PoC は **未ビルド・未フラッシュ**。

## ローカルでのビルド/検証手順 (Windows 等、GitHub に到達できる環境)

```bash
# 1. Xtensa 対応 Rust toolchain の導入 (初回のみ、~2GB のダウンロードが入る)
cargo install espup --locked
espup install --targets esp32
# 生成された export-esp.sh (Windows は export-esp.ps1) を毎回 source/実行する

# 2. 書き込みツール
cargo install espflash

# 3. ビルド
cd firmware-rust
cargo build --release

# 4. 書き込み + シリアルモニタ
cargo run --release   # espflash が (via .cargo/config.toml の runner 設定を足せば) flash+monitor を行う
# または
espflash flash --monitor target/xtensa-esp32-espidf/release/ble-medical-gateway-rust-poc
```

## 既知の未検証事項

- `esp32-nimble` の central 接続 API (`BLEDevice::new_client`, `Client::connect`,
  `Service::get_characteristic`, `Characteristic::on_notify`) が Nipro NT-100B /
  NBP-1BLE 実機に対して実際に動くかは未確認 (実機 + 到達可能な toolchain がある
  環境でのみ検証可能)。
- `sdkconfig.defaults` の NimBLE central-only 設定 (`CONFIG_BT_NIMBLE_ROLE_*`) は
  `platformio.ini` の `build_flags` を踏襲したが、ESP-IDF 側の Kconfig 名が
  NimBLE-Arduino のマクロ名と完全一致するかは未検証。
