#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_Sensor.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-1234-1234-abcdefabcdef"

Adafruit_LSM6DS3TRC imu;

BLECharacteristic *txCharacteristic;

bool deviceConnected = false;
bool oldDeviceConnected = false;


// 4 + 6*2 = 16 bytes
struct FootPacket {
  uint32_t timestamp;

  int16_t ax;
  int16_t ay;
  int16_t az;

  int16_t gx;
  int16_t gy;
  int16_t gz;
};


class ServerCallbacks : public BLEServerCallbacks {

  void onConnect(BLEServer *server) {
    deviceConnected = true;
    Serial.println("Wrist connected!");
  }

  void onDisconnect(BLEServer *server) {
    deviceConnected = false;
    Serial.println("Wrist disconnected!");
  }
};


void setup() {

  Serial.begin(115200);
  delay(2000);

  Serial.println("Starting Foot Full IMU + BLE...");

  // XIAO ESP32-C3
  // D4 = SDA
  // D5 = SCL
  Wire.begin(D4, D5);

  // ---------- IMU ----------
  if (!imu.begin_I2C(0x6A, &Wire)) {
    Serial.println("ERROR: Foot IMU not found!");

    while (1) {
      delay(10);
    }
  }

  Serial.println("Foot IMU found!");


  // ---------- BLE ----------
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

  sensors_event_t accel;
  sensors_event_t gyro;
  sensors_event_t temp;

  imu.getEvent(&accel, &gyro, &temp);


  if (deviceConnected) {

    FootPacket packet;

    packet.timestamp = millis();

    // Multiply by 100 before sending
    packet.ax = (int16_t)(accel.acceleration.x * 100.0f);
    packet.ay = (int16_t)(accel.acceleration.y * 100.0f);
    packet.az = (int16_t)(accel.acceleration.z * 100.0f);

    packet.gx = (int16_t)(gyro.gyro.x * 100.0f);
    packet.gy = (int16_t)(gyro.gyro.y * 100.0f);
    packet.gz = (int16_t)(gyro.gyro.z * 100.0f);


    txCharacteristic->setValue(
      (uint8_t *)&packet,
      sizeof(packet)
    );

    txCharacteristic->notify();


    // Debug print on Foot
    Serial.print("Sent | A: ");
    Serial.print(accel.acceleration.x);
    Serial.print(", ");
    Serial.print(accel.acceleration.y);
    Serial.print(", ");
    Serial.print(accel.acceleration.z);

    Serial.print(" | G: ");
    Serial.print(gyro.gyro.x);
    Serial.print(", ");
    Serial.print(gyro.gyro.y);
    Serial.print(", ");
    Serial.println(gyro.gyro.z);
  }


  // Restart advertising if Wrist disconnects
  if (!deviceConnected && oldDeviceConnected) {

    delay(500);

    BLEDevice::startAdvertising();

    Serial.println("Advertising restarted.");

    oldDeviceConnected = false;
  }


  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = true;
  }


  // 20 Hz for initial testing
  delay(50);
}