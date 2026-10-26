#include "../foot_hand_motion_test/HandMotionDetector.h"
#include "../wrist_hand_motion_test/HandEventTracker.h"
#include "../wrist_hand_motion_test/GaitStabilityDetector.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int testCount = 0;
template<class F> void test(const char* name, F body) {
  body(); ++testCount; std::cout << "PASS: " << name << '\n';
}
struct SensorRunner {
  HandMotionDetector motion;
  HandEventTracker tracker;
  GaitStabilityDetector timing;
  uint32_t now = 0;
  uint32_t sequence = 0;
  int events = 0;
  int alerts = 0;
  HandMotionDetector::Result latest;
  void sample(float acceleration = HandMotionDetector::Gravity, float gyro = 0.0f) {
    latest = motion.update(now, 0, 0, acceleration, 0, 0, gyro);
    if (latest.event) {
      ++events; ++sequence;
      HandTest::Packet packet = {HandTest::Magic, HandTest::Version, HandTest::Gesture,
          1, sequence, now, 0, 150};
      HandTest::Packet decoded;
      check(HandTest::decode(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet), decoded), "Protocol failed");
      auto decision = tracker.accept(decoded);
      if (decision.resetTiming) timing.reset();
      if (decision.event && timing.observeStride(decoded.timestampMs).alert) ++alerts;
    }
    now += 10;
  }
  void hold(uint32_t ms, float acceleration = HandMotionDetector::Gravity, float gyro = 0.0f) {
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) sample(acceleration, gyro);
  }
  void gesture(uint32_t cycleMs) {
    hold(100, HandMotionDetector::Gravity, 1.5f);
    hold(cycleMs - 100);
  }
};
HandTest::Packet packet(uint8_t type, uint32_t session, uint32_t sequence) {
  return {HandTest::Magic, HandTest::Version, type, session, sequence, 1000, 0, 0};
}
int main() {
  try {
    test("stationary IMU noise produces no events", [] {
      SensorRunner r;
      for (int i = 0; i < 500; ++i) r.sample(9.8f + (i % 3 - 1) * 0.1f, 0.04f);
      check(r.events == 0 && r.latest.armed, "Stationary input triggered");
    });
    test("one rotating gesture followed by a hold produces one event", [] {
      SensorRunner r; r.hold(300); r.gesture(1000);
      check(r.events == 1, "Incorrect gesture count");
    });
    test("acceleration-only movement can trigger an event", [] {
      SensorRunner r; r.hold(300); r.hold(100, 14.0f);
      check(r.events == 1, "Acceleration trigger missing");
    });
    test("a single-sample spike is rejected", [] {
      SensorRunner r; r.hold(300); r.sample(9.8f, 3.0f); r.hold(300);
      check(r.events == 0, "One-sample spike triggered");
    });
    test("continuous shaking without quiet rearm does not count as repeated gestures", [] {
      SensorRunner r; r.hold(300); r.hold(3000, 9.8f, 1.5f);
      check(r.events == 1, "Continuous active motion retriggered");
    });
    test("a sampling gap requires another quiet hold", [] {
      SensorRunner r; r.hold(300); r.now += 500; r.hold(100, 9.8f, 1.5f);
      check(r.events == 0, "Sampling gap retained armed state");
      r.hold(300); r.gesture(1000); check(r.events == 1, "Rearm after gap failed");
    });
    test("regular sensor pulses pass through BLE encoding into a stable baseline", [] {
      SensorRunner r; r.hold(300);
      for (int i = 0; i < 40; ++i) r.gesture(1000);
      check(r.events == 40 && r.timing.calibrated() && r.alerts == 0, "Real-input pipeline baseline failed");
    });
    test("irregular sensor pulse timing alerts and regular input recovers", [] {
      SensorRunner r; r.hold(300);
      for (int i = 0; i < 31; ++i) r.gesture(1000);
      const uint32_t periods[] = {450, 1450, 550, 1350, 950};
      for (int i = 0; i < 20; ++i) r.gesture(periods[i % 5]);
      check(r.alerts == 1 && r.timing.state() == GaitStabilityDetector::State::Unstable, "Irregular events did not alert once");
      for (int i = 0; i < 15; ++i) r.gesture(1000);
      check(r.timing.state() == GaitStabilityDetector::State::Stable, "Regular pulses did not recover");
    });
    test("heartbeats never become movement events", [] {
      HandEventTracker t;
      auto first = t.accept(packet(HandTest::Heartbeat, 1, 0));
      auto next = t.accept(packet(HandTest::Heartbeat, 1, 0));
      check(!first.event && !next.event && !next.resetTiming, "Heartbeat triggered event");
    });
    test("lost events and foot restart invalidate the baseline", [] {
      HandEventTracker t; t.accept(packet(HandTest::Heartbeat, 1, 0));
      auto one = t.accept(packet(HandTest::Gesture, 1, 1));
      auto three = t.accept(packet(HandTest::Gesture, 1, 3));
      auto restarted = t.accept(packet(HandTest::Gesture, 2, 1));
      check(one.event && !one.resetTiming, "First sequential event failed");
      check(three.event && three.resetTiming && three.missed == 1, "Missed event not caught");
      check(restarted.event && restarted.resetTiming, "Foot restart not caught");
    });
    test("heartbeat sequence changes reveal a missing event", [] {
      HandEventTracker t; t.accept(packet(HandTest::Gesture, 1, 1));
      auto heartbeat = t.accept(packet(HandTest::Heartbeat, 1, 2));
      check(!heartbeat.event && heartbeat.resetTiming && heartbeat.missed == 1, "Hidden loss not detected");
    });
    test("duplicates and old packets are ignored; sequence rollover works", [] {
      HandEventTracker t; t.accept(packet(HandTest::Gesture, 1, UINT32_MAX));
      auto repeated = t.accept(packet(HandTest::Gesture, 1, UINT32_MAX));
      auto wrapped = t.accept(packet(HandTest::Gesture, 1, 0));
      auto old = t.accept(packet(HandTest::Gesture, 1, UINT32_MAX));
      check(!repeated.event && wrapped.event && !wrapped.resetTiming && !old.event, "Sequence tracking failed");
    });
    test("wrong packet length, version, magic, and type are rejected", [] {
      auto p = packet(HandTest::Gesture, 1, 1); HandTest::Packet output;
      auto data = reinterpret_cast<const uint8_t*>(&p);
      check(!HandTest::decode(data, 16, output), "Old raw packet accepted");
      p.version = 99; check(!HandTest::decode(data, sizeof(p), output), "Wrong version accepted");
      p.version = HandTest::Version; p.magic = 0; check(!HandTest::decode(data, sizeof(p), output), "Wrong magic accepted");
      p.magic = HandTest::Magic; p.type = 99; check(!HandTest::decode(data, sizeof(p), output), "Wrong type accepted");
    });
    test("nonfinite IMU input is rejected", [] {
      SensorRunner r; r.hold(300); r.sample(std::numeric_limits<float>::quiet_NaN(), 2.0f);
      check(!r.latest.valid && r.events == 0, "Nonfinite input accepted");
    });
    std::cout << testCount << " hand-motion tests passed.\n";
  } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
