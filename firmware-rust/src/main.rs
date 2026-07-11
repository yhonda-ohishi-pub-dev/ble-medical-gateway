//! PoC: BLE scan -> central 接続の骨組み。
//!
//! 既存の Arduino 版 (`src/main.cpp`, NimBLE-Arduino) と同じ標準 GATT UUID
//! (Health Thermometer Service 0x1809 / Blood Pressure Service 0x1810) を対象に
//! esp32-nimble (ESP-IDF NimBLE component のラッパー) で scan -> connect ->
//! characteristic notify 購読までを行う。値のデコード/JSON シリアライズは
//! スコープ外 (Refs #1)。

use esp32_nimble::utilities::BleUuid;
use esp32_nimble::{BLEAdvertisedData, BLEAdvertisedDevice, BLEDevice, BLEScan};
use esp_idf_svc::hal::task::block_on;
use log::*;

const HEALTH_THERMOMETER_SERVICE: u16 = 0x1809;
const BLOOD_PRESSURE_SERVICE: u16 = 0x1810;
const TEMPERATURE_MEASUREMENT: u16 = 0x2A1C;
const BLOOD_PRESSURE_MEASUREMENT: u16 = 0x2A35;

const SCAN_DURATION_MS: i32 = 10_000;
const MIN_RSSI: i8 = -80;

fn main() -> anyhow::Result<()> {
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    block_on(async_main())
}

async fn async_main() -> anyhow::Result<()> {
    let ble_device = BLEDevice::take();
    let mut ble_scan = BLEScan::new();

    info!("scanning for thermometer/blood pressure monitor...");

    let target = ble_scan
        .active_scan(true)
        .interval(100)
        .window(99)
        .start(ble_device, SCAN_DURATION_MS, |device, data| {
            match_target_service(device, data).map(|service| (*device, service))
        })
        .await?;

    let Some((device, service_uuid)) = target else {
        info!("scan ended without a matching device");
        return Ok(());
    };

    info!(
        "found target device: addr={:?} rssi={}",
        device.addr(),
        device.rssi()
    );

    let measurement_uuid = if service_uuid == BleUuid::from_uuid16(HEALTH_THERMOMETER_SERVICE) {
        BleUuid::from_uuid16(TEMPERATURE_MEASUREMENT)
    } else {
        BleUuid::from_uuid16(BLOOD_PRESSURE_MEASUREMENT)
    };

    let mut client = ble_device.new_client();
    client.on_connect(|client| {
        // Arduino 版と同様、接続直後に conn params を更新する
        if let Err(e) = client.update_conn_params(120, 120, 0, 60) {
            warn!("update_conn_params failed: {e:?}");
        }
    });
    client.connect(&device.addr()).await?;
    info!("connected");

    let service = client.get_service(service_uuid).await?;
    let characteristic = service.get_characteristic(measurement_uuid).await?;

    if !characteristic.can_notify() {
        error!("characteristic can't notify: {characteristic}");
        client.disconnect()?;
        return Ok(());
    }

    characteristic
        .on_notify(|raw| {
            // PoC ではデコードせず raw bytes をそのまま出す
            // (体温/血圧のバイナリフォーマットのパースは Refs #1 のスコープ外)
            info!("notify: {raw:02x?}");
        })
        .subscribe_notify(false)
        .await?;

    info!("subscribed, waiting for notifications (this PoC never disconnects)");
    loop {
        esp_idf_svc::hal::delay::FreeRtos::delay_ms(1_000);
    }
}

/// 広告データが対象サービス (体温計/血圧計) を含み、かつ RSSI が閾値以上なら
/// そのサービス UUID を返す。
fn match_target_service(
    device: &BLEAdvertisedDevice,
    data: BLEAdvertisedData<&[u8]>,
) -> Option<BleUuid> {
    if device.rssi() < MIN_RSSI {
        return None;
    }

    let thermometer = BleUuid::from_uuid16(HEALTH_THERMOMETER_SERVICE);
    let blood_pressure = BleUuid::from_uuid16(BLOOD_PRESSURE_SERVICE);

    if data.is_advertising_service(&thermometer) {
        Some(thermometer)
    } else if data.is_advertising_service(&blood_pressure) {
        Some(blood_pressure)
    } else {
        None
    }
}
