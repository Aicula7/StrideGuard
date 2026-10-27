#include "../wrist_hand_motion_test/wrist_hand_motion_test.ino"
#include <iostream>
#include <stdexcept>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
uint32_t sampleSequence = 0;
uint32_t gestureSequence = 0;
uint32_t footTime = 10000;
void packet(uint8_t type, float accel = 0.15f, float gyro = 0.03f, bool runLoop = true) {
  hostNow = footTime;
  HandTest::Packet frame = {HandTest::Magic, HandTest::Version, type, 99,
      type == HandTest::MotionSample ? ++sampleSequence : gestureSequence,
      footTime, static_cast<int16_t>(accel * 100), static_cast<uint16_t>(gyro * 100)};
  receiveNotification(nullptr, reinterpret_cast<uint8_t*>(&frame), sizeof(frame), true);
  if (runLoop) loop();
}
void samples(uint32_t duration, float accel, float gyro) {
  for (uint32_t n = 0; n < duration; n += 20) { footTime += 20; packet(HandTest::MotionSample, accel, gyro); }
}
void command(char value) { Serial.input.push_back(value); Serial.input.push_back('\n'); loop(); }

int main() {
 try {
  setup(); linkUp.store(true); subscribed = true; lastPacketAt = millis();
  command('r');
  samples(2500, 5, 5);
  check(!timing.calibrated() && !alarmLatched && intensity.state() == VigorousMotionDetector::State::WaitingForData,
        "Pre-calibration samples triggered motion assessment");
  const uint32_t calibrationStart = footTime;
  for (int i = 0; i <= 1500; ++i) {
    footTime = calibrationStart + static_cast<uint32_t>(i) * 20;
    if (i % 50 == 0) { ++gestureSequence; packet(HandTest::Gesture); }
    packet(HandTest::MotionSample);
  }
  check(timing.calibrated() && timing.baselineIntervals() == 30, "Original calibration failed with interleaved samples");
  check(eventsReceived == 31 && !alarmLatched, "Wrong calibration event count or premature alarm");
  check(intensity.state() == VigorousMotionDetector::State::Collecting, "Post-baseline collection did not begin");
  samples(1500, 4.5f, 4);
  check(!alarmLatched && intensity.state() == VigorousMotionDetector::State::Suspect, "Alarm fired before fourth window");
  samples(500, 4.5f, 4);
  check(alarmLatched && drv.outputActive() && drv.mode == DRV2605_MODE_REALTIME, "Continuous alarm did not start");
  samples(1500, 4.5f, 4);
  check(eventsReceived == 31 && intensity.state() == VigorousMotionDetector::State::Unstable,
        "No gesture events incorrectly paused post-baseline motion detection");
  command('s');
  check(!alarmLatched && !drv.outputActive() && timing.baselineIntervals() == 30,
        "s did not stop RTP while retaining baseline");
  check(intensity.state() == VigorousMotionDetector::State::Unstable, "s forced an artificial recovery");
  samples(1000, 4.5f, 4);
  check(!alarmLatched, "Same ongoing episode immediately re-latched after s");
  samples(2000, 0.1f, 0.03f);
  check(intensity.state() == VigorousMotionDetector::State::Stable, "Quiet recovery failed");
  samples(2500, 4.5f, 4);
  check(alarmLatched, "A fresh post-recovery episode did not alert");
  samples(2000, 0.1f, 0.03f);
  check(intensity.state() == VigorousMotionDetector::State::Stable && alarmLatched && drv.outputActive(),
        "Recovery silenced an unacknowledged alarm");
  footTime += 600; packet(HandTest::Heartbeat);
  check(intensity.state() == VigorousMotionDetector::State::WaitingForData && !haveMotionSamples,
        "Healthy heartbeats disguised missing motion data");
  check(timing.baselineIntervals() == 30 && alarmLatched && drv.outputActive(), "Data gap cleared baseline or alarm");
  command('s'); command('v');
  check(drv.clicks == 1 && !alarmLatched && drv.mode == DRV2605_MODE_INTTRIG,
        "Short manual test failed after continuous stop");
  command('s');
  samples(2500, 4.5f, 4);
  check(alarmLatched, "Resumed samples could not start a new episode");
  for (int i = 0; i < 40; ++i) { footTime += 20; packet(HandTest::MotionSample, 5, 5, false); }
  loop();
  check(queueDrops.load() != 0 && intensity.state() == VigorousMotionDetector::State::WaitingForData,
        "Queue overflow did not invalidate motion evidence");
  check(timing.calibrated() && alarmLatched, "Overflow cleared baseline or alarm latch");
  const int priorScans = scanner->starts;
  client->disconnect(); loop();
  check(scanner->starts == priorScans && alarmLatched, "Blocking scans started while an alarm awaited acknowledgement");
  command('s');
  check(!alarmLatched && !drv.outputActive() && timing.calibrated(), "Stop failed while disconnected");
  linkUp.store(true); subscribed = true; lastPacketAt = millis();
  command('r');
  check(!timing.calibrated() && timing.baselineIntervals() == 0 && !alarmLatched && !haveMotionSamples,
        "r did not reset calibration and motion assessment");
  samples(2500, 6, 6);
  check(!alarmLatched && !timing.calibrated(), "New calibration was bypassed by vigorous input");
  check(Serial.output.find("data_state=COLLECTING_WINDOW") != std::string::npos &&
        Serial.output.find("VIGOROUS_MOTION_ALERT") != std::string::npos &&
        Serial.output.find("NO_CONTINUOUS_SAMPLES") != std::string::npos, "Required diagnostics missing");
  std::cout << "PASS: actual wrist sketch calibration with 50 Hz interleaving, independent sustained-motion alarm, s/r/v, recovery, same-episode acknowledgement, missing samples with heartbeats, queue overflow, and disconnected stop.\n";
 } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
