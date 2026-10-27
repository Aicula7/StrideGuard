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

// Calibration retains real FOOT gesture timing. After calibration, independent
// continuous FOOT IMU samples drive a fixed-threshold vigorous-motion demo.
Adafruit_DRV2605 drv;
bool hapticReady = false;
bool alarmLatched = false;
constexpr uint8_t AlarmRtpLevel = 0x40;  // Bench-test ERM drive level.
GaitStabilityDetector timing;  // Original 30-interval calibration, unchanged.
VigorousMotionDetector intensity;
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
uint32_t lastMotionSampleAt = 0;
uint32_t motionSamplesReceived = 0;
uint32_t motionDataGaps = 0;
uint32_t motionSamplesMissing = 0;
bool haveMotionSamples = false;

const char* experimentState() {
  return timing.calibrated() ? VigorousMotionDetector::stateName(intensity.state()) : "CALIBRATING";
}

const char* motionDataState() {
  if (!haveMotionSamples) return "NO_DATA";
  return timing.calibrated() ? intensity.dataStateName() : "STREAMING";
}

void resetMotionStream(const char* reason) {
  intensity.reset();
  haveMotionSamples = false;
  Serial.printf("# MOTION_DATA_PAUSED reason=%s. Baseline and alarm latch retained.\n", reason);
}

void printCalibrationProgress() {
  Serial.printf("# CALIBRATION intervals=%u/%u baseline_ready=%d\n",
      static_cast<unsigned>(timing.baselineIntervals()),
      static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget), timing.calibrated() ? 1 : 0);
}

void resetTiming(const char* reason) {
  timing.reset();
  intensity.reset();
  haveMotionSamples = false;
  motionSamplesReceived = 0;
  motionDataGaps = 0;
  motionSamplesMissing = 0;
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
  resetMotionStream(reason);
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

void processMotionSample(const HandTest::Packet& packet, uint32_t arrivedAt) {
  ++motionSamplesReceived;
  if (!timing.calibrated()) {
    // Receive the stream during calibration without changing its baseline.
    lastMotionSampleAt = arrivedAt;
    haveMotionSamples = true;
    return;
  }
  const auto result = intensity.observe(packet.timestampMs, packet.sequence,
      packet.accelerationDeviationCenti / 100.0f, packet.angularRateCenti / 100.0f);
  motionSamplesMissing += result.missedSamples;
  if (result.accepted) {
    lastMotionSampleAt = arrivedAt;
    haveMotionSamples = true;
  }
  if (result.streamReset) {
    ++motionDataGaps;
    Serial.printf("# MOTION_DATA_GAP reason=%s. Pending evidence cleared; baseline retained.\n", result.resetReason);
  }
  if (result.started) {
    Serial.println("# MOTION_DATA data_state=COLLECTING_WINDOW window_ms=0/500. No gesture or quiet hold is required.");
  }
  if (result.windowComplete) {
    Serial.printf("# MOTION_WINDOW duration_ms=%lu samples=%u accel_rms_mps2=%.2f gyro_rms_rps=%.2f accel_enter=%.2f gyro_enter=%.2f high=%d high_windows=%u/%u low_windows=%u/%u state=%s\n",
        static_cast<unsigned long>(result.durationMs), static_cast<unsigned>(result.samples),
        result.accelRms, result.gyroRms, VigorousMotionDetector::AccelEnter, VigorousMotionDetector::GyroEnter,
        result.high ? 1 : 0, static_cast<unsigned>(result.highWindows),
        static_cast<unsigned>(VigorousMotionDetector::EnterWindows), static_cast<unsigned>(result.lowWindows),
        static_cast<unsigned>(VigorousMotionDetector::ExitWindows), VigorousMotionDetector::stateName(result.state));
  }
  if (result.alert) {
    Serial.println("# VIGOROUS_MOTION_ALERT: four sustained high-intensity windows; bench demo, not a gait diagnosis.");
    startAlarm();
  }
}

void processPacket(const HandTest::Packet& packet, uint32_t arrivedAt) {
  lastPacketAt = arrivedAt;
  const bool footRestarted = haveFootSession && packet.bootSession != lastFootSession;
  haveFootSession = true;
  lastFootSession = packet.bootSession;
  if (footRestarted) {
    pauseTiming("FOOT_RESTART", true);
    resetMotionStream("FOOT_RESTART");
  }
  if (packet.type == HandTest::MotionSample) {
    processMotionSample(packet, arrivedAt);
    return;  // Its sequence is independent of gesture event sequencing.
  }
  const auto decision = tracker.accept(packet);
  if (decision.missed) {
    Serial.printf("# Missed gesture events: %lu\n", static_cast<unsigned long>(decision.missed));
    if (!timing.calibrated()) pauseTiming("MISSING_EVENTS", false);
  }
  if (!decision.event) return;
  eventSeen = true;
  lastEventAt = arrivedAt;
  ++eventsReceived;
  Serial.printf("# REAL_HAND_EVENT seq=%lu foot_ms=%lu dev_mps2=%.2f gyro_rps=%.2f state=%s\n",
      static_cast<unsigned long>(packet.sequence), static_cast<unsigned long>(packet.timestampMs),
      packet.accelerationDeviationCenti / 100.0f, packet.angularRateCenti / 100.0f, experimentState());
  if (timing.calibrated()) return;  // Event spacing no longer drives stability.

  const auto result = timing.observeStride(packet.timestampMs);
  if (result.gapReset) {
    ++pauseCount;
    lastPauseReason = "LONG_INTERVAL";
    Serial.printf("# INTERVAL_SKIPPED ms=%lu baseline_kept=%u/%u. Pause is not an instability alert.\n",
        static_cast<unsigned long>(result.intervalMs), static_cast<unsigned>(timing.baselineIntervals()),
        static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget));
  }
  if (result.referenceOnly) {
    Serial.printf("# REFERENCE_EVENT: calibration reference only. Next FOOT event must be %lu-%lu ms later.\n",
        static_cast<unsigned long>(GaitStabilityDetector::MinStrideMs),
        static_cast<unsigned long>(GaitStabilityDetector::MaxStrideMs));
  }
  if (result.accepted) Serial.printf("# INTERVAL_ACCEPTED ms=%lu\n", static_cast<unsigned long>(result.intervalMs));
  if (!result.accepted && !result.referenceOnly && !result.gapReset) {
    Serial.printf("# INTERVAL_SKIPPED too_short_ms=%lu\n", static_cast<unsigned long>(result.intervalMs));
  }
  printCalibrationProgress();
  if (result.windowComplete) {
    Serial.printf("# BASELINE_TIMING_WINDOW mean_ms=%.1f cv_pct=%.2f baseline_cv_pct=%.2f timing_enter_cv_pct=%.2f\n",
        result.meanMs, result.cvPercent, result.baselineCv, result.enterCv);
  }
  if (result.justCalibrated) {
    intensity.reset();
    Serial.println("# BASELINE_READY: 30 timing intervals saved. Switching to CONTINUOUS VIGOROUS MOTION detection.");
    Serial.println("# Fixed intensity thresholds are independent of timing CV. Move gently for STABLE; shake continuously for the alarm.");
    Serial.println("# Gesture events and quiet holds are no longer required. Four high 500 ms windows trigger the alarm; s stops it.");
    if (result.baselineCv > 10.0f) Serial.println("# BASELINE_NOISY: timing CV is high; it does not change the fixed motion thresholds.");
  }
}

void printHelp() {
  Serial.println("# HAND_TEST_BUILD: vigorous-motion-v2 protocol=2; upload BOTH matching sketches.");
  Serial.println("# r: stop and restart original 30-interval calibration; s: stop alarm, keep baseline; v: short motor test; ?: help");
  Serial.println("# After baseline: real 50 Hz motion samples, 500 ms RMS windows, four consecutive high windows to alert.");
  Serial.printf("# FIXED_DEMO_THRESHOLDS accel_enter=%.2f mps2 gyro_enter=%.2f rad/s accel_exit=%.2f gyro_exit=%.2f\n",
      VigorousMotionDetector::AccelEnter, VigorousMotionDetector::GyroEnter,
      VigorousMotionDetector::AccelExit, VigorousMotionDetector::GyroExit);
  Serial.println("# Thresholds are adjustable engineering starting values, not learned from the timing baseline.");
  Serial.println("# Alarm latches until s or r. Recovery/data loss do not silence it. s does not force STABLE.");
  Serial.printf("# Calibration: %u valid intervals, about %u uninterrupted gesture events. Pauses keep progress.\n",
      static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget),
      static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget + 1));
}

void printStatus(uint32_t now) {
  if (now - lastStatusAt >= 1000) {
    lastStatusAt = now;
    Serial.printf("# STATUS link=%d packet_age_ms=%lu state=%s baseline=%u/%u data_state=%s window_ms=%lu/%lu samples=%u event_activity=%s events_rx=%lu motion_samples_rx=%lu motion_gaps=%lu samples_missing=%lu alarm_latched=%d\n",
        linkUp.load() ? 1 : 0, static_cast<unsigned long>(now - lastPacketAt), experimentState(),
        static_cast<unsigned>(timing.baselineIntervals()),
        static_cast<unsigned>(GaitStabilityDetector::BaselineIntervalTarget), motionDataState(),
        static_cast<unsigned long>(intensity.windowElapsedMs()),
        static_cast<unsigned long>(VigorousMotionDetector::WindowMs), static_cast<unsigned>(intensity.windowSamples()),
        eventSeen && now - lastEventAt <= 2800 ? "RECENT_EVENT" : "NO_RECENT_EVENTS",
        static_cast<unsigned long>(eventsReceived), static_cast<unsigned long>(motionSamplesReceived),
        static_cast<unsigned long>(motionDataGaps), static_cast<unsigned long>(motionSamplesMissing), alarmLatched ? 1 : 0);
  }
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
      printStatus(now);
      delay(1);
      return;
    }
    if (static_cast<int32_t>(now - nextScanAt) >= 0) {
      connectFoot();
      nextScanAt = millis() + 3000;
    }
    printStatus(millis());
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
    pauseReception("NO_COMPATIBLE_FOOT_DATA_CHECK_BOTH_V2");
    subscribed = false;
    client->disconnect();
  } else if (!timing.calibrated() && eventSeen && now - lastEventAt > 2800) {
    pauseTiming("NO_MOTION", false);
  }
  if (haveMotionSamples && now - lastMotionSampleAt > VigorousMotionDetector::NoDataTimeoutMs) {
    resetMotionStream("NO_CONTINUOUS_SAMPLES");
  }
  printStatus(now);
  delay(1);
}
