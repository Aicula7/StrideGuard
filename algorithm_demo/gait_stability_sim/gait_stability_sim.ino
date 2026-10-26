#include <Arduino.h>
#include "GaitStabilityDetector.h"

// Default: serial-only simulation, no IMU, BLE peer, or motor required.
// Set to 1 only with an actual DRV2605L and ERM motor connected.
#define ENABLE_HAPTICS 0
#if ENABLE_HAPTICS
#include <Wire.h>
#include <Adafruit_DRV2605.h>
Adafruit_DRV2605 drv;
bool hapticReady = false;
#endif

GaitStabilityDetector detector;
const uint16_t normalIntervals[] = {750, 760, 740, 755, 745};
const uint16_t unstableIntervals[] = {500, 1000, 550, 950, 750};
bool unstableScenario = false;
bool paused = false;
bool singleDisturbance = false;
size_t patternIndex = 0;
uint32_t simulatedTimeMs = 0;
uint32_t nextDueMs = 0;
uint16_t pendingIntervalMs = 750;

void printHelp() {
  Serial.println("# SIMULATION ONLY: generated same-foot strike timestamps, not IMU measurements.");
  Serial.println("# n: normal/recovery; u: sustained unstable timing; s: one disturbed interval");
  Serial.println("# p: pause/resume; r: reset/calibrate; v: manual output test; ?: help");
  Serial.println("# Calibration: 30 normal intervals, about 22.5 seconds.");
  Serial.println("# Monitoring: 5 intervals per non-overlapping window; 2 high windows to alert.");
  Serial.println("# Thresholds are baseline-relative experimental settings, not validated cutoffs.");
  Serial.println("sim_ms,scenario,stride_ms,window_complete,cv_pct,baseline_cv_pct,enter_cv_pct,state,alert");
}

void playHaptic() {
#if ENABLE_HAPTICS
  if (hapticReady) {
    drv.setWaveform(0, 1);
    drv.setWaveform(1, 0);
    drv.go();
    Serial.println("# HAPTIC_COMMAND_SENT");
    return;
  }
#endif
  Serial.println("# HAPTIC_SIMULATED: no motor command sent.");
}

void scheduleNext() {
  if (singleDisturbance) {
    pendingIntervalMs = 1100;
    singleDisturbance = false;
  } else {
    pendingIntervalMs = unstableScenario ? unstableIntervals[patternIndex % 5]
                                         : normalIntervals[patternIndex % 5];
    ++patternIndex;
  }
  nextDueMs = millis() + pendingIntervalMs;
}

void resetSimulation() {
  detector.reset();
  simulatedTimeMs = 0;
  detector.observeStride(simulatedTimeMs);
  unstableScenario = false;
  paused = false;
  singleDisturbance = false;
  patternIndex = 0;
  scheduleNext();
  Serial.println("# RESET: normal scenario; calibrating for about 22.5 seconds.");
}

void handleCommand(char command) {
  switch (command) {
    case 'n': case 'N':
      unstableScenario = false;
      singleDisturbance = false;
      patternIndex = 0;
      scheduleNext();
      Serial.println("# SCENARIO: NORMAL/RECOVERY (existing baseline retained)");
      break;
    case 'u': case 'U': case 's': case 'S':
      if (!detector.calibrated()) {
        Serial.println("# Wait for BASELINE_READY before injecting a disturbance.");
        break;
      }
      unstableScenario = command == 'u' || command == 'U';
      singleDisturbance = !unstableScenario;
      patternIndex = 0;
      scheduleNext();
      Serial.println(unstableScenario ? "# SCENARIO: UNSTABLE" : "# SCENARIO: SINGLE_DISTURBANCE then normal");
      break;
    case 'p': case 'P':
      paused = !paused;
      if (paused) {
        detector.interrupt();
        Serial.println("# PAUSED: valid calibration progress and baseline retained.");
      } else {
        detector.observeStride(simulatedTimeMs);
        scheduleNext();
        Serial.println("# RESUMED: next generated event provides a fresh interval.");
      }
      break;
    case 'r': case 'R': resetSimulation(); break;
    case 'v': case 'V':
      Serial.println("# MANUAL_OUTPUT_TEST: independent of the detection algorithm.");
      playHaptic();
      break;
    case '?': printHelp(); break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
#if ENABLE_HAPTICS
  Wire.begin();  // Must match the actual wrist board's SDA/SCL wiring.
  hapticReady = drv.begin();
  if (hapticReady) {
    drv.useERM();
    drv.selectLibrary(1);
    drv.setMode(DRV2605_MODE_INTTRIG);
  } else {
    Serial.println("# DRV2605L not found; continuing with serial-only simulation.");
  }
#endif
  printHelp();
  resetSimulation();
}

void loop() {
  while (Serial.available()) handleCommand(static_cast<char>(Serial.read()));
  if (paused || static_cast<int32_t>(millis() - nextDueMs) < 0) return;
  // Synthetic timestamps advance by known test intervals, not measured motion.
  simulatedTimeMs += pendingIntervalMs;
  const auto result = detector.observeStride(simulatedTimeMs);
  Serial.print(simulatedTimeMs);
  Serial.print(unstableScenario ? ",UNSTABLE," : ",NORMAL_OR_SINGLE,");
  Serial.print(pendingIntervalMs);
  Serial.print(',');
  Serial.print(result.windowComplete ? 1 : 0);
  Serial.print(',');
  if (result.windowComplete) Serial.print(result.cvPercent, 2);
  else Serial.print("NA");
  Serial.print(',');
  Serial.print(result.baselineCv, 2);
  Serial.print(',');
  Serial.print(result.enterCv, 2);
  Serial.print(',');
  Serial.print(GaitStabilityDetector::stateName(result.state));
  Serial.print(',');
  Serial.println(result.alert ? 1 : 0);
  if (result.justCalibrated) Serial.println("# BASELINE_READY: use u, s, or n to run a test.");
  if (result.alert) {
    Serial.println("# ALERT: persistent baseline-relative stride-timing variability.");
    playHaptic();
  }
  if (result.cooldownSuppressed) Serial.println("# ALERT_SUPPRESSED: cooldown active.");
  scheduleNext();
}
