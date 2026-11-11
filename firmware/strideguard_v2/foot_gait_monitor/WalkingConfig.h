#pragma once

// Physical sensor axis pointing along the shoe (heel <-> toe).
// 'x' is only a placeholder until you inspect your mounting orientation.
constexpr char DefaultRollAxis = 'x';
// Leave false until the physical axis above has been checked. Normal timing
// and impact detection still run. Send x/y/z from the WRIST serial port to
// select/confirm an axis for this powered session; this also resets baseline.
constexpr bool DefaultRollAxisConfirmed = false;
constexpr uint32_t SamplePeriodMs = 10;  // approximately 100 Hz detector
constexpr uint32_t RawPeriodMs = 20;     // approximately 50 Hz wireless trace
constexpr uint32_t StartupSettleMs = 3000; // after first receiver connection
