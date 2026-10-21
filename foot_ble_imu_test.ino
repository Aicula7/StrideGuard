#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_Sensor.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------- BLE UUIDs ----------
// Must match Wrist
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-1234-1234-abcdefabcdef"

// ---------- IMU ----------
Adafruit_LSM6DS3TRC imu;

// ---------- BLE ----------
BLECharacteristic *txCharacteristic;

bool deviceConnected = false;
bool oldDeviceConnected = false;


// Three floats = 12 bytes
struct FootAccelData {
  float ax;
  float ay;
  float az;
};


class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
    Serial.println("Wrist connected!");
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    Serial.println("Wrist disconnected!");
  }
};


void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("Starting Foot IMU + BLE...");

  // ---------------- I2C ----------------
  // XIAO ESP32-C3:
  // D4 = SDA
  // D5 = SCL
  Wire.begin(D4, D5);

  // ---------------- IMU ----------------
  if (!imu.begin_I2C(0x6A, &Wire)) {
    Serial.println("ERROR: Foot IMU not found!");

    while (1) {
      delay(10);
    }
  }

  Serial.println("Foot IMU found!");

  // ---------------- BLE ----------------
  BLEDevice::init("StrideGuard_Foot");

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service =
    server->createService(SERVICE_UUID);

  txCharacteristic =
    service->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_NOTIFY
    );

  txCharacteristic->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *advertising =
    BLEDevice::getAdvertising();

  advertising->addServiceUUID(SERVICE_UUID);

  BLEDevice::startAdvertising();

  Serial.println("Foot BLE advertising...");
}


void loop() {

  // ---------- Read Foot IMU ----------
  sensors_event_t accel;
  sensors_event_t gyro;
  sensors_event_t temp;

  imu.getEvent(&accel, &gyro, &temp);

  if (deviceConnected) {

    FootAccelData data;

    data.ax = accel.acceleration.x;
    data.ay = accel.acceleration.y;
    data.az = accel.acceleration.z;

    // Send raw 12 bytes by BLE
    txCharacteristic->setValue(
      (uint8_t *)&data,
      sizeof(data)
    );

    txCharacteristic->notify();

    // Also show what Foot sent
    Serial.print("Sent Accel: X=");
    Serial.print(data.ax);
    Serial.print("  Y=");
    Serial.print(data.ay);
    Serial.print("  Z=");
    Serial.println(data.az);
  }

  // Restart advertising after disconnect
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);

    BLEDevice::startAdvertising();

    Serial.println("Advertising restarted.");

    oldDeviceConnected = false;
  }

  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = true;
  }

  // First BLE test rate: 10 Hz
  delay(100);
}