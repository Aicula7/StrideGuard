#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_DRV2605.h>
#include <BLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include "HandTestProtocol.h"
#include "HandEventTracker.h"
#include "GaitStabilityDetector.h"

// All detector input comes from the real FOOT IMU gesture events.
// This sketch does not generate synthetic events or use wrist motion as gait.
Adafruit_DRV2605 drv;
bool hapticReady = false;
GaitStabilityDetector timing;
HandEventTracker tracker;
BLEClient* client = nullptr;
BLEScan* scanner = nullptr;
std::atomic<bool> linkUp(false);
std::atomic<bool> resetLink(false);
std::atomic<BLEAdvertisedDevice*> discovered(nullptr);
std::atomic<uint32_t> queueDrops(0);
bool subscribed = false;
struct ReceivedPacket {
  HandTest::Packet packet;
  uint32_t arrivedAt;
};
QueueHandle_t rxQueue = nullptr;
uint32_t lastPacketAt = 0;
uint32_t lastEventAt = 0;
uint32_t nextScanAt = 0;
uint32_t lastStatusAt = 0;
uint32_t reportedDrops = 0;
bool eventSeen = false;

void resetTiming(const char* reason) {
  timing.reset();
  eventSeen = false;
  Serial.println(reason);
}

void resetReception(const char* reason) {
  resetTiming(reason);
  tracker.reset();
  if (rxQueue) xQueueReset(rxQueue);
}

void vibrate() {
  if (!hapticReady) {
    Serial.println("# HAPTIC_UNAVAILABLE: serial alert only; no motor command sent.");
    return;
  }
  drv.setWaveform(0, 1);
  drv.setWaveform(1, 0);
  drv.go();
  Serial.println("# HAPTIC_COMMAND_SENT");
}

void receiveNotification(BLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  ReceivedPacket received;
  if (!HandTest::decode(data, length, received.packet)) return;
  received.arrivedAt = millis();
  if (xQueueSend(rxQueue, &received, 0) != pdTRUE) queueDrops.fetch_add(1);
}

class HandClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient*) override { linkUp.store(true); }
  void onDisconnect(BLEClient*) override {
    linkUp.store(false);
    resetLink.store(true);
  }
};

class HandScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) override {
    if (!device.haveServiceUUID() ||
        !device.isAdvertisingService(BLEUUID(HandTest::ServiceUuid))) return;
    BLEAdvertisedDevice* candidate = new BLEAdvertisedDevice(device);
    BLEAdvertisedDevice* expected = nullptr;
    if (discovered.compare_exchange_strong(expected, candidate)) scanner->stop();
    else delete candidate;
  }
};

bool connectFoot() {
  Serial.println("# Scanning for the FOOT hand-test service...");
  scanner->start(2, false);
  BLEAdvertisedDevice* target = discovered.exchange(nullptr);
  bool ready = false;
  if (target && client->connect(target)) {
    BLERemoteService* service = client->getService(BLEUUID(HandTest::ServiceUuid));
    BLERemoteCharacteristic* characteristic = service ?
        service->getCharacteristic(BLEUUID(HandTest::CharacteristicUuid)) : nullptr;
    if (characteristic && characteristic->canNotify()) {
      characteristic->registerForNotify(receiveNotification);
      ready = true;
    } else {
      client->disconnect();
    }
  }
  delete target;
  scanner->clearResults();
  if (ready) {
    subscribed = true;
    lastPacketAt = millis();
    Serial.println("# LINK_READY: real foot events only. Move foot regularly about once per second.");
  }
  return ready;
}

void processPacket(const HandTest::Packet& packet, uint32_t arrivedAt) {
  lastPacketAt = arrivedAt;
  const auto decision = tracker.accept(packet);
  if (decision.resetTiming) {
    resetTiming("# FOOT_SESSION_OR_EVENT_GAP: restarting motion-timing baseline.");
    if (decision.missed) Serial.printf("# Missed events: %lu\n", static_cast<unsigned long>(decision.missed));
  }
  if (!decision.event) return;  // Heartbeats are never treated as motion events.
  eventSeen = true;
  lastEventAt = arrivedAt;
  const auto result = timing.observeStride(packet.timestampMs);
  Serial.printf("# REAL_HAND_EVENT seq=%lu foot_ms=%lu dev_mps2=%.2f gyro_rps=%.2f state=%s\n",
      static_cast<unsigned long>(packet.sequence), static_cast<unsigned long>(packet.timestampMs),
      packet.accelerationDeviationCenti / 100.0f, packet.angularRateCenti / 100.0f,
      GaitStabilityDetector::stateName(result.state));
  if (result.gapReset) Serial.println("# MOTION_INTERVAL_GAP: a pause is not classified as unstable motion.");
  if (!timing.calibrated()) {
    const size_t intervals = result.baselineWindows * GaitStabilityDetector::WindowSize +
                            (result.windowComplete ? 0 : result.windowCount);
    Serial.printf("# CALIBRATION intervals=%u/30. Keep a steady rhythm.\n", static_cast<unsigned>(intervals));
  }
  if (result.windowComplete) {
    Serial.printf("# WINDOW mean_ms=%.1f cv_pct=%.2f baseline_cv_pct=%.2f enter_cv_pct=%.2f state=%s\n",
        result.meanMs, result.cvPercent, result.baselineCv, result.enterCv,
        GaitStabilityDetector::stateName(result.state));
  }
  if (result.justCalibrated) {
    Serial.println("# BASELINE_READY: now vary your HAND gesture intervals while keeping separate gestures.");
    if (result.baselineCv > 10.0f) {
      Serial.println("# BASELINE_NOISY: consider r and recalibrate with a steadier rhythm.");
    }
  }
  if (result.alert) {
    Serial.println("# HAND_TIMING_ALERT: sustained irregular real foot-module gestures; not a validated gait diagnosis.");
    vibrate();
  }
  if (result.cooldownSuppressed) Serial.println("# ALERT_SUPPRESSED: cooldown active.");
}

void printHelp() {
  Serial.println("# MANUAL_HAND_TEST: wrist receives real FOOT IMU gesture events, no synthetic input.");
  Serial.println("# r: restart timing calibration; v: manual motor test; ?: help");
  Serial.println("# Wrist IMU is not used for this bench test. No-motion or lost-data states reset timing, not alert.");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  printHelp();
  Wire.begin();  // Same actual wrist-board I2C wiring as the full hardware test.
  hapticReady = drv.begin();
  if (hapticReady) {
    drv.useERM();
    drv.selectLibrary(1);
    drv.setMode(DRV2605_MODE_INTTRIG);
    Serial.println("# HAPTIC_READY");
  } else {
    Serial.println("# DRV2605L not found; continuing with serial alerts only.");
  }
  rxQueue = xQueueCreate(32, sizeof(ReceivedPacket));
  if (!rxQueue) {
    Serial.println("# ERROR: notification queue allocation failed.");
    while (true) delay(100);
  }
  BLEDevice::init("StrideGuard_Wrist_HandTest");
  client = BLEDevice::createClient();
  client->setClientCallbacks(new HandClientCallbacks());
  scanner = BLEDevice::getScan();
  scanner->setAdvertisedDeviceCallbacks(new HandScanCallbacks());
  scanner->setActiveScan(true);
}

void loop() {
  uint32_t now = millis();
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command == 'r' || command == 'R') resetReception("# RESET: build a new normal HAND-motion baseline.");
    if (command == 'v' || command == 'V') {
      Serial.println("# MANUAL_MOTOR_TEST: independent of the detector.");
      vibrate();
    }
    if (command == '?') printHelp();
  }
  if (resetLink.exchange(false)) {
    subscribed = false;
    resetReception("# DISCONNECTED: timing invalidated; reconnecting.");
  }
  if (!linkUp.load() || !subscribed) {
    if (static_cast<int32_t>(now - nextScanAt) >= 0) {
      connectFoot();
      nextScanAt = millis() + 3000;
    }
    delay(1);
    return;
  }
  const uint32_t drops = queueDrops.load();
  if (drops != reportedDrops) {
    reportedDrops = drops;
    resetReception("# RECEIVE_OVERFLOW: timing invalidated, recalibration required.");
  }
  ReceivedPacket received;
  while (xQueueReceive(rxQueue, &received, 0) == pdTRUE) {
    processPacket(received.packet, received.arrivedAt);
  }
  now = millis();
  if (now - lastPacketAt > 2500) {
    resetReception("# NO_FOOT_DATA: not a motion alert; reconnecting.");
    subscribed = false;
    client->disconnect();
  } else if (eventSeen && now - lastEventAt > 2800) {
    resetTiming("# NO_MOTION: baseline reset. Resume regular gestures to calibrate again.");
  }
  if (now - lastStatusAt >= 2000) {
    lastStatusAt = now;
    Serial.printf("# STATUS link=%d packet_age_ms=%lu state=%s\n", linkUp.load() ? 1 : 0,
        static_cast<unsigned long>(now - lastPacketAt), GaitStabilityDetector::stateName(timing.state()));
  }
  delay(1);
}
