/**
 * BLE Medical Device Gateway for ATOM Lite
 *
 * Connects to Nipro BLE thermometer (NT-100B) and blood pressure monitor (NBP-1BLE)
 * and sends data to PC via serial (COM port)
 */

#include <M5Atom.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLESecurity.h>

// BLE Standard Service UUIDs
static BLEUUID healthThermometerServiceUUID((uint16_t)0x1809);
static BLEUUID bloodPressureServiceUUID((uint16_t)0x1810);

// BLE Standard Characteristic UUIDs
static BLEUUID temperatureMeasurementUUID((uint16_t)0x2A1C);
static BLEUUID bloodPressureMeasurementUUID((uint16_t)0x2A35);

// Debug mode flag - set to false for Web Serial production use
bool debugMode = false;

// Helper function for debug output
void debugPrint(const char* message) {
    if (debugMode) {
        Serial.println(message);
    }
}

void debugPrintf(const char* format, ...) {
    if (debugMode) {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        Serial.print(buffer);
    }
}

// Device states
bool thermometerFound = false;
bool bloodPressureFound = false;
bool thermometerConnected = false;
bool bloodPressureConnected = false;

// BLE objects
BLEScan* pBLEScan;
BLEClient* pThermometerClient;
BLEClient* pBloodPressureClient;

// Device addresses (will be discovered during scan)
BLEAdvertisedDevice* thermometerDevice = nullptr;
BLEAdvertisedDevice* bloodPressureDevice = nullptr;

// LED colors
const CRGB COLOR_SCANNING = CRGB::Blue;
const CRGB COLOR_CONNECTED = CRGB::Green;
const CRGB COLOR_DATA_RECEIVED = CRGB::White;
const CRGB COLOR_ERROR = CRGB::Red;

void setLED(CRGB color) {
    M5.dis.drawpix(0, color);
}

// Parse temperature measurement data (IEEE 11073 FLOAT format)
float parseTemperature(uint8_t* data, size_t length) {
    if (length < 5) return -1.0;

    // First byte is flags
    uint8_t flags = data[0];
    bool isFahrenheit = flags & 0x01;

    // Bytes 1-4 contain temperature in IEEE 11073 FLOAT format
    int32_t mantissa = (int32_t)((data[1]) | (data[2] << 8) | (data[3] << 16));
    if (mantissa & 0x800000) {
        mantissa |= 0xFF000000; // Sign extend
    }
    int8_t exponent = (int8_t)data[4];

    float temperature = (float)mantissa * pow(10.0, exponent);

    // Convert to Celsius if needed
    if (isFahrenheit) {
        temperature = (temperature - 32.0) * 5.0 / 9.0;
    }

    return temperature;
}

// Parse blood pressure measurement data
void parseBloodPressure(uint8_t* data, size_t length, float* systolic, float* diastolic, float* pulse) {
    if (length < 7) {
        *systolic = *diastolic = *pulse = -1.0;
        return;
    }

    uint8_t flags = data[0];
    bool isKPa = flags & 0x01;
    bool hasTimestamp = flags & 0x02;
    bool hasPulseRate = flags & 0x04;
    bool hasUserId = flags & 0x08;
    bool hasMeasurementStatus = flags & 0x10;

    // IEEE 11073 SFLOAT format (16-bit)
    auto parseSFLOAT = [](uint8_t lo, uint8_t hi) -> float {
        int16_t mantissa = (int16_t)((lo) | ((hi & 0x0F) << 8));
        if (mantissa & 0x0800) mantissa |= 0xF000; // Sign extend
        int8_t exponent = (int8_t)(hi >> 4);
        if (exponent & 0x08) exponent |= 0xF0; // Sign extend
        return (float)mantissa * pow(10.0, exponent);
    };

    *systolic = parseSFLOAT(data[1], data[2]);
    *diastolic = parseSFLOAT(data[3], data[4]);
    // Bytes 5-6: Mean Arterial Pressure (not used in output)

    // Calculate pulse rate offset based on optional fields present
    // Base offset after systolic/diastolic/MAP = 7
    size_t offset = 7;
    if (hasTimestamp) offset += 7;  // Timestamp is 7 bytes
    if (hasPulseRate && (offset + 2) <= length) {
        *pulse = parseSFLOAT(data[offset], data[offset + 1]);
    } else {
        *pulse = -1.0;
    }

    // Convert kPa to mmHg if needed
    if (isKPa) {
        *systolic *= 7.50062;
        *diastolic *= 7.50062;
    }
}

// Temperature notification callback
void temperatureNotifyCallback(BLERemoteCharacteristic* pCharacteristic, uint8_t* data, size_t length, bool isNotify) {
    setLED(COLOR_DATA_RECEIVED);

    float temperature = parseTemperature(data, length);

    // Output JSON to serial
    Serial.printf("{\"type\":\"temperature\",\"value\":%.1f,\"unit\":\"celsius\"}\n", temperature);

    delay(100);
    setLED(COLOR_CONNECTED);
}

// Blood pressure indication callback
void bloodPressureNotifyCallback(BLERemoteCharacteristic* pCharacteristic, uint8_t* data, size_t length, bool isNotify) {
    setLED(COLOR_DATA_RECEIVED);

    float systolic, diastolic, pulse;
    parseBloodPressure(data, length, &systolic, &diastolic, &pulse);

    // Output JSON to serial
    if (pulse > 0) {
        Serial.printf("{\"type\":\"blood_pressure\",\"systolic\":%.0f,\"diastolic\":%.0f,\"pulse\":%.0f,\"unit\":\"mmHg\"}\n",
                      systolic, diastolic, pulse);
    } else {
        Serial.printf("{\"type\":\"blood_pressure\",\"systolic\":%.0f,\"diastolic\":%.0f,\"unit\":\"mmHg\"}\n",
                      systolic, diastolic);
    }

    delay(100);
    setLED(COLOR_CONNECTED);
}

// BLE Advertised Device Callback
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        // Scan results only in debug mode
        debugPrintf("{\"type\":\"scan\",\"name\":\"%s\",\"address\":\"%s\",\"rssi\":%d}\n",
                      advertisedDevice.getName().c_str(),
                      advertisedDevice.getAddress().toString().c_str(),
                      advertisedDevice.getRSSI());

        // Check for Health Thermometer Service
        if (advertisedDevice.haveServiceUUID() &&
            advertisedDevice.isAdvertisingService(healthThermometerServiceUUID)) {
            debugPrint("{\"type\":\"found\",\"device\":\"thermometer\"}");
            thermometerDevice = new BLEAdvertisedDevice(advertisedDevice);
            thermometerFound = true;
        }

        // Check for Blood Pressure Service
        if (advertisedDevice.haveServiceUUID() &&
            advertisedDevice.isAdvertisingService(bloodPressureServiceUUID)) {
            debugPrint("{\"type\":\"found\",\"device\":\"blood_pressure\"}");
            bloodPressureDevice = new BLEAdvertisedDevice(advertisedDevice);
            bloodPressureFound = true;
        }

        // Also check by device name (Nipro devices might use custom names)
        String name = advertisedDevice.getName().c_str();
        if (name.indexOf("NT-100") >= 0 || name.indexOf("Thermo") >= 0) {
            if (!thermometerFound) {
                debugPrint("{\"type\":\"found\",\"device\":\"thermometer\",\"by\":\"name\"}");
                thermometerDevice = new BLEAdvertisedDevice(advertisedDevice);
                thermometerFound = true;
            }
        }
        if (name.indexOf("NBP-1") >= 0 || name.indexOf("BP") >= 0 || name.indexOf("Blood") >= 0) {
            if (!bloodPressureFound) {
                debugPrint("{\"type\":\"found\",\"device\":\"blood_pressure\",\"by\":\"name\"}");
                bloodPressureDevice = new BLEAdvertisedDevice(advertisedDevice);
                bloodPressureFound = true;
            }
        }
    }
};

// Connect to thermometer
bool connectToThermometer() {
    if (thermometerDevice == nullptr) return false;

    debugPrint("{\"type\":\"connecting\",\"device\":\"thermometer\"}");

    pThermometerClient = BLEDevice::createClient();

    if (!pThermometerClient->connect(thermometerDevice)) {
        debugPrint("{\"type\":\"error\",\"message\":\"Failed to connect to thermometer\"}");
        return false;
    }

    // Get the service
    BLERemoteService* pService = pThermometerClient->getService(healthThermometerServiceUUID);
    if (pService == nullptr) {
        debugPrint("{\"type\":\"error\",\"message\":\"Thermometer service not found\"}");
        pThermometerClient->disconnect();
        return false;
    }

    // Get the characteristic
    BLERemoteCharacteristic* pCharacteristic = pService->getCharacteristic(temperatureMeasurementUUID);
    if (pCharacteristic == nullptr) {
        debugPrint("{\"type\":\"error\",\"message\":\"Temperature characteristic not found\"}");
        pThermometerClient->disconnect();
        return false;
    }

    // Register for notifications/indications
    if (pCharacteristic->canIndicate()) {
        pCharacteristic->registerForNotify(temperatureNotifyCallback, false); // false = indication
        debugPrint("{\"type\":\"debug\",\"message\":\"Registered for indication\"}");
    } else if (pCharacteristic->canNotify()) {
        pCharacteristic->registerForNotify(temperatureNotifyCallback, true); // true = notification
        debugPrint("{\"type\":\"debug\",\"message\":\"Registered for notification\"}");
    } else {
        debugPrint("{\"type\":\"error\",\"message\":\"Characteristic cannot notify or indicate\"}");
    }

    debugPrint("{\"type\":\"connected\",\"device\":\"thermometer\"}");
    thermometerConnected = true;
    return true;
}

// Connect to blood pressure monitor
bool connectToBloodPressure() {
    if (bloodPressureDevice == nullptr) return false;

    debugPrint("{\"type\":\"connecting\",\"device\":\"blood_pressure\"}");
    debugPrintf("{\"type\":\"debug\",\"address\":\"%s\"}\n", bloodPressureDevice->getAddress().toString().c_str());

    pBloodPressureClient = BLEDevice::createClient();
    pBloodPressureClient->setClientCallbacks(nullptr);

    // Try to connect with longer timeout
    for (int retry = 0; retry < 3; retry++) {
        debugPrintf("{\"type\":\"debug\",\"message\":\"Connect attempt %d\"}\n", retry + 1);
        if (pBloodPressureClient->connect(bloodPressureDevice)) {
            debugPrint("{\"type\":\"debug\",\"message\":\"Connection successful\"}");
            break;
        }
        if (retry == 2) {
            debugPrint("{\"type\":\"error\",\"message\":\"Failed to connect to blood pressure monitor after 3 attempts\"}");
            return false;
        }
        delay(1000);
    }

    // Get the service
    BLERemoteService* pService = pBloodPressureClient->getService(bloodPressureServiceUUID);
    if (pService == nullptr) {
        debugPrint("{\"type\":\"error\",\"message\":\"Blood pressure service not found\"}");
        pBloodPressureClient->disconnect();
        return false;
    }

    // Get the characteristic
    BLERemoteCharacteristic* pCharacteristic = pService->getCharacteristic(bloodPressureMeasurementUUID);
    if (pCharacteristic == nullptr) {
        debugPrint("{\"type\":\"error\",\"message\":\"Blood pressure characteristic not found\"}");
        pBloodPressureClient->disconnect();
        return false;
    }

    // Register for notifications/indications
    if (pCharacteristic->canIndicate()) {
        pCharacteristic->registerForNotify(bloodPressureNotifyCallback, false); // false = indication
        debugPrint("{\"type\":\"debug\",\"message\":\"BP registered for indication\"}");
    } else if (pCharacteristic->canNotify()) {
        pCharacteristic->registerForNotify(bloodPressureNotifyCallback, true); // true = notification
        debugPrint("{\"type\":\"debug\",\"message\":\"BP registered for notification\"}");
    } else {
        debugPrint("{\"type\":\"error\",\"message\":\"BP characteristic cannot notify or indicate\"}");
    }

    debugPrint("{\"type\":\"connected\",\"device\":\"blood_pressure\"}");
    bloodPressureConnected = true;
    return true;
}

void setup() {
    // Initialize M5Atom
    M5.begin(true, false, true); // Serial, I2C, LED

    Serial.begin(115200);
    delay(1000);

    // Always output ready message on startup
    Serial.println("{\"type\":\"ready\",\"device\":\"ATOM Lite BLE Gateway\",\"version\":\"1.0.0\"}");

    debugPrint("{\"type\":\"startup\",\"device\":\"ATOM Lite BLE Gateway\"}");

    // Initialize BLE
    BLEDevice::init("ATOM_Medical_Gateway");

    // Setup BLE Security for pairing (Just Works mode)
    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    debugPrint("{\"type\":\"debug\",\"message\":\"BLE security configured (Just Works)\"}");

    // Create BLE Scanner
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    setLED(COLOR_SCANNING);
    debugPrint("{\"type\":\"status\",\"message\":\"Starting BLE scan...\"}");
}

void loop() {
    M5.update();

    // Button press to restart scan and toggle debug mode
    if (M5.Btn.wasPressed()) {
        debugPrint("{\"type\":\"status\",\"message\":\"Button pressed, restarting scan...\"}");
        thermometerFound = false;
        bloodPressureFound = false;
        thermometerConnected = false;
        bloodPressureConnected = false;
        if (pThermometerClient != nullptr && pThermometerClient->isConnected()) {
            pThermometerClient->disconnect();
        }
        if (pBloodPressureClient != nullptr && pBloodPressureClient->isConnected()) {
            pBloodPressureClient->disconnect();
        }
        setLED(COLOR_SCANNING);
    }

    // Scan for devices if not found
    if (!thermometerFound || !bloodPressureFound) {
        setLED(COLOR_SCANNING);
        BLEScanResults foundDevices = pBLEScan->start(5, false); // Scan for 5 seconds
        pBLEScan->clearResults();
    }

    // Connect to found devices
    if (thermometerFound && !thermometerConnected) {
        connectToThermometer();
    }

    if (bloodPressureFound && !bloodPressureConnected) {
        connectToBloodPressure();
    }

    // Update LED based on connection status
    if (thermometerConnected || bloodPressureConnected) {
        setLED(COLOR_CONNECTED);
    }

    // Check connection status periodically
    if (thermometerConnected && pThermometerClient != nullptr && !pThermometerClient->isConnected()) {
        debugPrint("{\"type\":\"disconnected\",\"device\":\"thermometer\"}");
        thermometerConnected = false;
        thermometerFound = false;
    }

    if (bloodPressureConnected && pBloodPressureClient != nullptr && !pBloodPressureClient->isConnected()) {
        debugPrint("{\"type\":\"disconnected\",\"device\":\"blood_pressure\"}");
        bloodPressureConnected = false;
        bloodPressureFound = false;
    }

    delay(100);
}
