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
bool alarmLatched = false;
constexpr uint8_t AlarmRtpLevel = 0x40;  // Bench-test ERM drive level.
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
uint32_t eventsReceived = 0;
uint32_t pauseCount = 0;
const char* lastPauseReason = "NONE";
bool haveFootSession = false;
uint32_t lastFootSession = 0;

void printCalibrationProgress() {
  Serial.printf("# CALIBRATION intervals=%u/%u baseline_ready=%d\n",
      static_cast<unsigned>(timing.baselineIntervals()),
      static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget), timing.calibrated() ? 1 : 0);
}

void resetTiming(const char* reason) {
  timing.reset();
  eventSeen = false;
  eventsReceived = 0;
  pauseCount = 0;
  lastPauseReason = "MANUAL_RESET";
  Serial.println(reason);
  printCalibrationProgress();
}

void resetReception(const char* reason) {
  resetTiming(reason);
  tracker.reset();
  if (rxQueue) xQueueReset(rxQueue);
}

void pauseTiming(const char* reason, bool clockRestarted) {
  timing.interrupt(clockRestarted);
  eventSeen = false;
  ++pauseCount;
  lastPauseReason = reason;
  Serial.printf("# TIMING_PAUSED reason=%s baseline_kept=%u/%u. Next event is a new time reference.\n",
      reason, static_cast<unsigned>(timing.baselineIntervals()),
      static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget));
}

void pauseReception(const char* reason) {
  pauseTiming(reason, false);
  if (rxQueue) xQueueReset(rxQueue);
}

void vibrate() {
  if (alarmLatched) {
    Serial.println("# ALARM_ALREADY_LATCHED: send s to stop; manual click skipped.");
    return;
  }
  if (!hapticReady) {
    Serial.println("# HAPTIC_UNAVAILABLE: serial alert only; no motor command sent.");
    return;
  }
  drv.setWaveform(0, 1);
  drv.setWaveform(1, 0);
  drv.go();
  Serial.println("# HAPTIC_COMMAND_SENT");
}

void startAlarm() {
  if (alarmLatched) return;
  alarmLatched = true;
  Serial.println("# ALARM_LATCHED: continuous vibration until s; recovery and pauses do not silence it.");
  if (!hapticReady) {
    Serial.println("# HAPTIC_UNAVAILABLE: alarm latched in software; no motor command sent.");
    return;
  }
  drv.stop();
  drv.setRealtimeValue(0);
  drv.setMode(DRV2605_MODE_REALTIME);
  drv.setRealtimeValue(AlarmRtpLevel);
  Serial.println("# HAPTIC_COMMAND_SENT mode=CONTINUOUS; send s to stop.");
}

void stopAlarm() {
  if (hapticReady) {
    // RTP is stopped by writing zero, not just clearing the waveform GO bit.
    drv.setRealtimeValue(0);
    drv.stop();
    drv.setMode(DRV2605_MODE_INTTRIG);
  }
  alarmLatched = false;
  Serial.println("# ALARM_STOPPED: baseline retained; detector continues monitoring.");
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
    Serial.println("# LINK_READY: real foot events only. Tilt once, then hold still; repeat about every 1.5 s.");
  }
  return ready;
}

void processPacket(const HandTest::Packet& packet, uint32_t arrivedAt) {
  lastPacketAt = arrivedAt;
  const bool footRestarted = haveFootSession && packet.bootSession != lastFootSession;
  haveFootSession = true;
  lastFootSession = packet.bootSession;
  const auto decision = tracker.accept(packet);
  if (decision.resetTiming) {
    if (footRestarted) pauseTiming("FOOT_RESTART", true);
    else if (decision.missed) pauseTiming("MISSING_EVENTS", false);
    if (decision.missed) Serial.printf("# Missed events: %lu\n", static_cast<unsigned long>(decision.missed));
  }
  if (!decision.event) return;  // Heartbeats are never treated as motion events.
  eventSeen = true;
  lastEventAt = arrivedAt;
  ++eventsReceived;
  const auto result = timing.observeStride(packet.timestampMs);
  Serial.printf("# REAL_HAND_EVENT seq=%lu foot_ms=%lu dev_mps2=%.2f gyro_rps=%.2f state=%s\n",
      static_cast<unsigned long>(packet.sequence), static_cast<unsigned long>(packet.timestampMs),
      packet.accelerationDeviationCenti / 100.0f, packet.angularRateCenti / 100.0f,
      GaitStabilityDetector::stateName(result.state));
  if (result.gapReset) {
    ++pauseCount;
    lastPauseReason = "LONG_INTERVAL";
    Serial.printf("# INTERVAL_SKIPPED ms=%lu baseline_kept=%u/%u. Pause is not an instability alert.\n",
        static_cast<unsigned long>(result.intervalMs), static_cast<unsigned>(timing.baselineIntervals()),
        static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget));
  }
  if (result.referenceOnly) {
    Serial.printf("# REFERENCE_EVENT: start timestamp only, no interval yet. Next FOOT event must be %lu-%lu ms later.\n",
        static_cast<unsigned long>(GaitStabilityDetector::MinStrideMs),
        static_cast<unsigned long>(GaitStabilityDetector::MaxStrideMs));
  }
  if (result.accepted) {
    Serial.printf("# INTERVAL_ACCEPTED ms=%lu\n", static_cast<unsigned long>(result.intervalMs));
  }
  if (!result.accepted && !result.referenceOnly && !result.gapReset) {
    Serial.printf("# INTERVAL_SKIPPED too_short_ms=%lu\n", static_cast<unsigned long>(result.intervalMs));
  }
  if (!timing.calibrated() || result.justCalibrated) printCalibrationProgress();
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
    startAlarm();
  }
  if (result.cooldownSuppressed) Serial.println("# ALERT_SUPPRESSED: cooldown active.");
}

void printHelp() {
  Serial.println("# HAND_TEST_BUILD: latched-alarm-30-v1");
  Serial.println("# MANUAL_HAND_TEST: wrist receives real FOOT IMU gesture events, no synthetic input.");
  Serial.println("# s: stop latched vibration; r: stop and recalibrate; v: one short motor test; ?: help");
  Serial.println("# Automatic alerts latch until s or r, even if timing recovers or FOOT disconnects.");
  Serial.println("# s keeps the baseline and does not retrigger the same ongoing episode.");
  Serial.println("# Pauses/lost data keep valid calibration samples and baseline; only r or wrist restart clears them.");
  Serial.println("# A skipped interval never counts toward calibration. Wrist IMU is not used for this bench test.");
  Serial.printf("# Calibration target: %u valid intervals. First event is a time reference.\n",
      static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget));
  Serial.println("# First verify 5 separate tilts give 5 FOOT events. Hold still between tilts; do not continuously wave.");
  Serial.printf("# Then send r ONCE and repeat a tilt-and-hold cycle about every 1.5 s for %u events.\n",
      static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget + 1));
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
    if (command == 's' || command == 'S') stopAlarm();
    if (command == 'r' || command == 'R') {
      stopAlarm();
      resetReception("# RESET: build a new normal HAND-motion baseline.");
    }
    if (command == 'v' || command == 'V') {
      Serial.println("# MANUAL_MOTOR_TEST: independent of the detector.");
      vibrate();
    }
    if (command == '?') printHelp();
  }
  if (resetLink.exchange(false)) {
    subscribed = false;
    pauseReception("DISCONNECTED");
  }
  if (!linkUp.load() || !subscribed) {
    // Keep the stop command responsive: do not enter blocking BLE discovery
    // or connection attempts while an unacknowledged alarm is vibrating.
    if (alarmLatched) {
      delay(1);
      return;
    }
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
    pauseReception("RECEIVE_OVERFLOW");
  }
  ReceivedPacket received;
  while (xQueueReceive(rxQueue, &received, 0) == pdTRUE) {
    processPacket(received.packet, received.arrivedAt);
  }
  now = millis();
  if (now - lastPacketAt > 2500) {
    pauseReception("NO_FOOT_DATA");
    subscribed = false;
    client->disconnect();
  } else if (eventSeen && now - lastEventAt > 2800) {
    pauseTiming("NO_MOTION", false);
  }
  if (now - lastStatusAt >= 2000) {
    lastStatusAt = now;
    Serial.printf("# STATUS link=%d packet_age_ms=%lu state=%s baseline=%u/%u activity=%s events_rx=%lu pauses=%lu last_pause=%s alarm_latched=%d\n",
        linkUp.load() ? 1 : 0, static_cast<unsigned long>(now - lastPacketAt),
        GaitStabilityDetector::stateName(timing.state()), static_cast<unsigned>(timing.baselineIntervals()),
        static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget),
        eventSeen ? "ACTIVE" : "IDLE", static_cast<unsigned long>(eventsReceived),
        static_cast<unsigned long>(pauseCount), lastPauseReason, alarmLatched ? 1 : 0);
  }
  delay(1);
}
