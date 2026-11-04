#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "PacketDef.h"

Adafruit_LSM6DS3TRC lsm6ds3;
BLEServer* pServer = nullptr;
BLECharacteristic* pFootCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

unsigned long lastSampleTime = 0;
const unsigned long SAMPLE_INTERVAL_MS = 20; // 50 Hz

class FootServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!lsm6ds3.begin_I2C(0x6A)) {
    Serial.println("Error: Foot LSM6DS3 not detected at 0x6A!");
    while (1) { delay(10); }
  }

  // Set IMU dynamic ranges
  lsm6ds3.setAccelRange(LSM6DS_ACCEL_RANGE_4_G);
  lsm6ds3.setGyroRange(LSM6DS_GYRO_RANGE_500_DPS);
  lsm6ds3.setAccelDataRate(LSM6DS_RATE_104_HZ);
  lsm6ds3.setGyroDataRate(LSM6DS_RATE_104_HZ);

  // Initialize BLE Server
  BLEDevice::init("StrideGuard_Foot");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new FootServerCallbacks());

  BLEService* pService = pServer->createService(FOOT_SERVICE_UUID);
  pFootCharacteristic = pService->createCharacteristic(
    FOOT_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pFootCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(FOOT_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  Serial.println("StrideGuard_Foot advertising 16-byte packed BLE packets...");
}

void loop() {
  unsigned long currentMillis = millis();

  if (deviceConnected && (currentMillis - lastSampleTime >= SAMPLE_INTERVAL_MS)) {
    lastSampleTime = currentMillis;

    sensors_event_t accel, gyro, temp;
    lsm6ds3.getEvent(&accel, &gyro, &temp);

    ImuEventPacket packet;
    packet.ax = (int16_t)(accel.acceleration.x * 100.0f);
    packet.ay = (int16_t)(accel.acceleration.y * 100.0f);
    packet.az = (int16_t)(accel.acceleration.z * 100.0f);
    packet.gx = (int16_t)(gyro.gyro.x * 100.0f);
    packet.gy = (int16_t)(gyro.gyro.y * 100.0f);
    packet.gz = (int16_t)(gyro.gyro.z * 100.0f);
    packet.temp = (int16_t)(temp.temperature * 100.0f);
    packet.flags = 0; // Default status

    pFootCharacteristic->setValue((uint8_t*)&packet, sizeof(packet));
    pFootCharacteristic->notify();
  }

  // Handle automatic re-advertising on disconnect
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Foot pod re-advertising...");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
}
