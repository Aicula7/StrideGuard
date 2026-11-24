#pragma once

// Physical sensor axis pointing along the shoe (heel <-> toe).
// Change to 'x', 'y', or 'z' (or select in the iOS Devices tab) to match your shoe mount.
constexpr char DefaultRollAxis = 'x';
constexpr bool DefaultRollAxisConfirmed = true;
constexpr uint32_t SamplePeriodMs = 10;          // ~100 Hz detector
constexpr uint32_t RawPeriodMs = 20;             // ~50 Hz wireless trace
constexpr uint32_t StartupSettleMs = 3000;       // standstill window for gyro zero-bias & gravity calibration
constexpr float TurnYawSuppressionDps = 140.0f;  // suppress false triggers during pivot turns
