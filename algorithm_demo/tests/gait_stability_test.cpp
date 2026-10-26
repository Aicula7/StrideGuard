#include "../gait_stability_sim/GaitStabilityDetector.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using Detector = GaitStabilityDetector;
const uint32_t normal[] = {750, 760, 740, 755, 745};
const uint32_t unstable[] = {500, 1000, 550, 950, 750};
struct Runner {
  Detector detector;
  uint32_t time;
  int alerts = 0;
  explicit Runner(uint32_t start = 0) : time(start) { detector.observeStride(time); }
  Detector::Result step(uint32_t dt) {
    time += dt;
    auto result = detector.observeStride(time);
    if (result.alert) ++alerts;
    return result;
  }
  Detector::Result feed(const uint32_t* pattern, size_t length, int count) {
    Detector::Result result;
    for (int i = 0; i < count; ++i) result = step(pattern[i % length]);
    return result;
  }
  Detector::Result calibrate() { return feed(normal, 5, 30); }
};
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
int tests = 0;
template<class F> void test(const char* name, F body) {
  body();
  ++tests;
  std::cout << "PASS: " << name << '\n';
}
int main() {
  try {
    test("30-interval calibration and baseline-relative thresholds", [] {
      Runner r;
      r.feed(normal, 5, 29);
      check(!r.detector.calibrated(), "Calibration completed too early");
      auto result = r.step(normal[4]);
      check(result.justCalibrated && r.detector.calibrated(), "Calibration did not complete");
      check(result.baselineCv > 0.9f && result.baselineCv < 1.0f, "Unexpected baseline CV");
      check(result.enterCv > result.exitCv && result.exitCv > result.baselineCv, "Invalid hysteresis");
      check(r.alerts == 0, "Alert during calibration");
    });
    test("sustained normal timing produces no alerts", [] {
      Runner r; r.calibrate();
      auto result = r.feed(normal, 5, 60);
      check(r.alerts == 0 && result.state == Detector::State::Stable, "Normal timing misclassified");
    });
    test("one disturbed stride produces no alert", [] {
      Runner r; r.calibrate(); r.step(1100);
      auto result = r.feed(normal, 5, 19);
      check(r.alerts == 0 && result.state == Detector::State::Stable, "Single disturbance caused alert");
    });
    test("two independent high-CV windows produce one alert", [] {
      Runner r; r.calibrate();
      auto first = r.feed(unstable, 5, 5);
      check(first.state == Detector::State::Suspect && r.alerts == 0, "First window alerted");
      auto second = r.feed(unstable, 5, 5);
      check(second.alert && second.state == Detector::State::Unstable && r.alerts == 1, "Missing confirmed alert");
      r.feed(unstable, 5, 30);
      check(r.alerts == 1, "Repeated alert without recovery");
    });
    test("two normal windows recover and a later episode can alert", [] {
      Runner r; r.calibrate(); r.feed(unstable, 5, 10);
      auto first = r.feed(normal, 5, 5);
      check(first.state == Detector::State::Unstable, "Recovered too early");
      auto second = r.feed(normal, 5, 5);
      check(second.state == Detector::State::Stable, "Failed to recover");
      r.feed(unstable, 5, 10);
      check(r.alerts == 2, "Later episode did not alert after cooldown");
    });
    test("changing to another steady pace is not sustained instability", [] {
      Runner r; r.calibrate();
      const uint32_t faster[] = {650};
      auto result = r.feed(faster, 1, 20);
      check(r.alerts == 0 && result.state == Detector::State::Stable, "Steady pace change caused alert");
    });
    test("duplicate and short bounce do not move the valid timestamp", [] {
      Runner r; r.calibrate();
      check(!r.detector.observeStride(r.time).accepted, "Duplicate accepted");
      check(!r.detector.observeStride(r.time + 100).accepted, "Bounce accepted");
      auto result = r.step(750);
      check(result.accepted && result.intervalMs == 750, "Bounce corrupted interval");
    });
    test("long gap invalidates the window and restarts calibration", [] {
      Runner r; r.calibrate(); r.feed(unstable, 5, 5);
      auto gap = r.step(5000);
      check(gap.gapReset && !gap.alert && !r.detector.calibrated(), "Gap treated as gait");
      r.calibrate();
      check(r.detector.calibrated() && r.alerts == 0, "Recalibration failed");
    });
    test("event timestamp rollover preserves valid intervals", [] {
      Runner r(UINT32_MAX - 1000);
      auto result = r.calibrate();
      check(result.justCalibrated && result.intervalMs == 745 && r.alerts == 0, "Rollover failed");
    });
    test("different baseline variability changes the alert threshold", [] {
      Runner a; auto low = a.calibrate();
      Runner b;
      const uint32_t variable[] = {700, 800, 750, 775, 725};
      auto high = b.feed(variable, 5, 30);
      check(high.enterCv > low.enterCv && high.baselineCv > low.baselineCv, "Threshold is not personalized");
    });
    test("new episode inside cooldown changes state without another alert", [] {
      Runner r;
      const uint32_t base[] = {350};
      const uint32_t varied[] = {350, 500, 350, 500, 350};
      r.feed(base, 1, 30);
      r.feed(varied, 5, 10);
      check(r.alerts == 1, "First fast episode missing");
      r.feed(base, 1, 10);
      auto result = r.feed(varied, 5, 10);
      check(result.state == Detector::State::Unstable && result.cooldownSuppressed && r.alerts == 1, "Cooldown failed");
    });
    std::cout << tests << " tests passed.\n";
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
