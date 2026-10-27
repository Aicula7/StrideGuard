#include "../wrist_hand_motion_test/GaitStabilityDetector.h"
#include "../wrist_hand_motion_test/HandEventTracker.h"
#include "../foot_hand_motion_test/HandMotionDetector.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using Motion = VigorousMotionDetector;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int tests = 0;
template<class F> void test(const char* name, F body) {
  body(); ++tests; std::cout << "PASS: " << name << '\n';
}
struct Runner {
  Motion motion;
  Motion::Result result;
  uint32_t now;
  uint32_t sequence;
  int alerts = 0;
  int windows = 0;
  explicit Runner(float accel = 0, float gyro = 0, uint32_t time = 0, uint32_t seq = 0)
      : now(time), sequence(seq) { sample(accel, gyro); }
  void sample(float accel, float gyro) {
    HandTest::Packet input = {HandTest::Magic, HandTest::Version, HandTest::MotionSample,
        7, sequence++, now, static_cast<int16_t>(accel * 100), static_cast<uint16_t>(gyro * 100)};
    HandTest::Packet decoded;
    check(HandTest::decode(reinterpret_cast<const uint8_t*>(&input), sizeof(input), decoded), "Sample decode failed");
    result = motion.observe(decoded.timestampMs, decoded.sequence,
        decoded.accelerationDeviationCenti / 100.0f, decoded.angularRateCenti / 100.0f);
    alerts += result.alert;
    windows += result.windowComplete;
    now += 20;
  }
  void feed(uint32_t ms, float accel = 0, float gyro = 0) {
    for (uint32_t n = 0; n < ms; n += 20) sample(accel, gyro);
  }
};

int main() {
 try {
  test("no data and partial data are not reported as stable", [] {
    Motion m;
    check(m.state() == Motion::State::WaitingForData, "Missing data was not distinct");
    auto r = m.observe(0, 1, 0, 0);
    check(r.started && r.state == Motion::State::Collecting, "First sample not collecting");
    for (uint32_t t = 20; t < 500; t += 20) m.observe(t, t / 20 + 1, 0, 0);
    check(m.state() == Motion::State::Collecting, "Partial window was called stable");
  });
  test("stationary real-format samples complete stable half-second windows", [] {
    Runner r(0.15f, 0.03f); r.feed(2000, 0.15f, 0.03f);
    check(r.windows == 4 && r.alerts == 0 && r.motion.state() == Motion::State::Stable, "Quiet input misclassified");
    check(r.result.samples == 25 && r.result.durationMs == 500, "Incorrect window coverage");
  });
  test("ordinary low-intensity motion stays stable", [] {
    Runner r(1.0f, 1.8f); r.feed(6000, 1.0f, 1.8f);
    check(r.alerts == 0 && r.motion.state() == Motion::State::Stable, "Ordinary motion alerted");
  });
  test("four consecutive acceleration windows alert only on the fourth", [] {
    Runner r(4, 0.1f); r.feed(1500, 4, 0.1f);
    check(r.windows == 3 && r.alerts == 0 && r.motion.state() == Motion::State::Suspect, "Alert was premature");
    r.feed(500, 4, 0.1f);
    check(r.alerts == 1 && r.motion.state() == Motion::State::Unstable, "Sustained motion failed");
    r.feed(5000, 4, 0.1f);
    check(r.alerts == 1, "Continuing episode repeatedly alerted");
  });
  test("sustained rotation can independently trigger the alarm", [] {
    Runner r(0.1f, 4); r.feed(2000, 0.1f, 4);
    check(r.alerts == 1 && r.motion.state() == Motion::State::Unstable, "Rotation path failed");
  });
  test("one large short spike does not become a sustained alarm", [] {
    Runner r; r.sample(40, 8); r.feed(3000);
    check(r.alerts == 0 && r.motion.state() == Motion::State::Stable, "Transient spike alerted");
  });
  test("a low window breaks pending consecutive high evidence", [] {
    Runner r(4); r.feed(1500, 4); r.feed(500); r.feed(1500, 4);
    check(r.alerts == 0, "Separated high segments combined");
  });
  test("hysteresis keeps an episode active until four low windows", [] {
    Runner r(4, 4); r.feed(2000, 4, 4); r.feed(2000, 2, 2);
    check(r.motion.state() == Motion::State::Unstable, "Mid-band input recovered");
    r.feed(1500);
    check(r.motion.state() == Motion::State::Unstable, "Recovered before four low windows");
    r.feed(500);
    check(r.motion.state() == Motion::State::Stable, "Did not recover");
    r.feed(2500, 4, 4);
    check(r.alerts == 2, "A fresh episode did not alert");
  });
  test("negative acceleration deviation contributes positive RMS energy", [] {
    Runner r(-4); r.feed(2000, -4);
    check(r.alerts == 1 && r.result.accelRms > 3.9f, "Signed acceleration was mishandled");
  });
  test("a burst loss clears partial windows and pending evidence", [] {
    Runner r(4); r.feed(1500, 4);
    r.sequence += 10; r.now += 200; r.sample(4, 0);
    check(r.result.streamReset && r.motion.state() == Motion::State::Collecting, "Burst did not invalidate evidence");
    r.feed(1500, 4);
    check(r.alerts == 0, "Alert evidence crossed data loss");
  });
  test("small isolated loss is allowed when coverage remains sufficient", [] {
    Runner r(4);
    for (int i = 1; i <= 110; ++i) {
      if (i % 10 == 0) { ++r.sequence; r.now += 20; }
      else r.sample(4, 0);
    }
    check(r.alerts == 1, "Small packet losses prevented all valid windows");
  });
  test("low-rate undersampled windows cannot trigger", [] {
    Runner r(10, 8);
    for (int i = 0; i < 100; ++i) { ++r.sequence; r.now += 20; r.sample(10, 8); }
    check(r.alerts == 0 && r.motion.state() == Motion::State::Collecting, "Sparse data produced an alarm");
  });
  test("duplicates and out-of-order samples do not add evidence", [] {
    Motion m; m.observe(100, 10, 4, 4);
    check(!m.observe(100, 10, 4, 4).accepted, "Duplicate accepted");
    check(!m.observe(80, 9, 4, 4).accepted, "Old sample accepted");
    check(m.windowSamples() == 1, "Repeated packets inflated sample count");
  });
  test("invalid sensor values clear evidence without producing an alert", [] {
    Runner r(4); r.feed(1500, 4);
    auto bad = r.motion.observe(r.now, r.sequence, std::numeric_limits<float>::quiet_NaN(), 4);
    check(!bad.accepted && bad.streamReset && r.motion.state() == Motion::State::WaitingForData, "NaN was accepted");
    check(!r.motion.observe(r.now, r.sequence, 4, -1).accepted, "Negative magnitude accepted");
  });
  test("explicit data timeout or restart requires fresh evidence", [] {
    Runner r(4); r.feed(1500, 4); r.motion.reset();
    check(r.motion.state() == Motion::State::WaitingForData, "Reset retained an old decision");
    r.feed(1500, 4);
    check(r.alerts == 0, "Old high windows survived reset");
  });
  test("timestamp and sequence rollover preserve complete windows", [] {
    Runner r(4, 4, UINT32_MAX - 200, UINT32_MAX - 50); r.feed(2000, 4, 4);
    check(r.alerts == 1 && r.windows == 4, "Rollover corrupted motion stream");
  });
  test("motion stream sequence never interferes with gesture calibration", [] {
    HandEventTracker tracker;
    HandTest::Packet p = {HandTest::Magic, HandTest::Version, HandTest::Heartbeat, 1, 0, 0, 0, 0};
    tracker.accept(p);
    p.type = HandTest::MotionSample; p.sequence = 9999;
    const auto sample = tracker.accept(p);
    check(!sample.event && !sample.resetTiming, "Sample became a gesture");
    p.type = HandTest::Gesture; p.sequence = 1;
    const auto gesture = tracker.accept(p);
    check(gesture.event && !gesture.resetTiming, "Sample sequence corrupted gesture tracker");
  });
  test("continuous shaking alerts while gesture events stay at one", [] {
    HandMotionDetector gestures;
    for (uint32_t t = 0; t < 300; t += 20) gestures.update(t, 0, 0, HandMotionDetector::Gravity, 0, 0, 0);
    Runner r(4, 4); int events = 0;
    for (uint32_t t = 300; t < 2400; t += 20) {
      events += gestures.update(t, 0, 0, HandMotionDetector::Gravity + 4, 0, 0, 4).event;
      r.sample(4, 4);
    }
    check(events == 1 && r.alerts == 1, "Motion alarm still depended on gesture rearming");
  });
  std::cout << tests << " vigorous-motion tests passed.\n";
 } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
