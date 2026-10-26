#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_system.h>
#include <atomic>
#include "HandMotionDetector.h"
#include "HandTestProtocol.h"

Adafruit_LSM6DS3TRC imu;
HandMotionDetector motion;
BLECharacteristic* tx = nullptr;
std::atomic<bool> connected(false);
std::atomic<bool> restartAdvertising(false);
uint32_t bootSession = 0;
uint32_t eventSequence = 0;
uint32_t lastSampleMs = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t lastStatusMs = 0;
HandMotionDetector::Result latest;

class HandServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { connected.store(true); }
  void onDisconnect(BLEServer*) override {
    connected.store(false);
    restartAdvertising.store(true);
  }
};

void notifyPacket(uint8_t type, uint32_t timestampMs) {
  if (!connected.load()) return;
  HandTest::Packet packet = {};
  packet.magic = HandTest::Magic;
  packet.version = HandTest::Version;
  packet.type = type;
  packet.bootSession = bootSession;
  packet.sequence = eventSequence;
  packet.timestampMs = timestampMs;
  const float accel = fmaxf(-32768.0f, fminf(32767.0f, latest.accelerationDeviation * 100.0f));
  const float gyro = fmaxf(0.0f, fminf(65535.0f, latest.angularRateMagnitude * 100.0f));
  packet.accelerationDeviationCenti = static_cast<int16_t>(accel);
  packet.angularRateCenti = static_cast<uint16_t>(gyro);
  tx->setValue(reinterpret_cast<uint8_t*>(&packet), sizeof(packet));
  tx->notify();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("# MANUAL_HAND_TEST: real foot IMU input, not validated gait detection.");
  Wire.begin(D4, D5);  // XIAO ESP32-C3: D4=SDA, D5=SCL.
  if (!imu.begin_I2C(0x6A, &Wire)) {
    Serial.println("# ERROR: Foot IMU not found at 0x6A.");
    while (true) delay(100);
  }
  imu.setAccelRange(LSM6DS_ACCEL_RANGE_4_G);
  imu.setGyroRange(LSM6DS_GYRO_RANGE_500_DPS);
  imu.setAccelDataRate(LSM6DS_RATE_104_HZ);
  imu.setGyroDataRate(LSM6DS_RATE_104_HZ);
  // Random boot identity detects restarts; all motion measurements are real.
  bootSession = esp_random();
  BLEDevice::init("StrideGuard_Foot_HandTest");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new HandServerCallbacks());
  BLEService* service = server->createService(HandTest::ServiceUuid);
  tx = service->createCharacteristic(HandTest::CharacteristicUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  tx->addDescriptor(new BLE2902());
  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(HandTest::ServiceUuid);
  BLEDevice::startAdvertising();
  Serial.println("# FOOT_READY: advertising hand-test service. Hold still, then make separate move-and-hold gestures.");
}

void loop() {
  const uint32_t now = millis();
  if (restartAdvertising.exchange(false)) {
    BLEDevice::startAdvertising();
    Serial.println("# BLE advertising restarted.");
  }
  if (now - lastSampleMs >= 10) {
    lastSampleMs = now;
    sensors_event_t acceleration, angularRate, temperature;
    imu.getEvent(&acceleration, &angularRate, &temperature);
    latest = motion.update(now, acceleration.acceleration.x, acceleration.acceleration.y,
        acceleration.acceleration.z, angularRate.gyro.x, angularRate.gyro.y, angularRate.gyro.z);
    if (latest.event) {
      ++eventSequence;
      notifyPacket(HandTest::Gesture, now);
      Serial.printf("# HAND_EVENT seq=%lu foot_ms=%lu dev_mps2=%.2f gyro_rps=%.2f connected=%d\n",
          static_cast<unsigned long>(eventSequence), static_cast<unsigned long>(now),
          latest.accelerationDeviation, latest.angularRateMagnitude, connected.load() ? 1 : 0);
    }
  }
  if (latest.valid && now - lastHeartbeatMs >= 500) {
    lastHeartbeatMs = now;
    notifyPacket(HandTest::Heartbeat, now);
  }
  if (now - lastStatusMs >= 1000) {
    lastStatusMs = now;
    Serial.printf("# SENSOR dev_mps2=%.2f gyro_rps=%.2f armed=%d connected=%d events=%lu\n",
        latest.accelerationDeviation, latest.angularRateMagnitude, latest.armed ? 1 : 0,
        connected.load() ? 1 : 0, static_cast<unsigned long>(eventSequence));
  }
  delay(1);
}
