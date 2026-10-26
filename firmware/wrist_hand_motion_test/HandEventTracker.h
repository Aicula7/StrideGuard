#pragma once
#include "HandTestProtocol.h"

// Separates actual gesture events from heartbeats and rejects duplicates.
// Missing events and foot restarts break timing continuity; callers retain calibration.
class HandEventTracker {
 public:
  struct Decision {
    bool event = false;
    bool resetTiming = false;  // Break the time reference, not the saved baseline.
    uint32_t missed = 0;
  };
  void reset() { seen_ = false; }
  Decision accept(const HandTest::Packet& packet) {
    Decision out;
    if (!seen_ || packet.bootSession != session_) {
      seen_ = true;
      session_ = packet.bootSession;
      sequence_ = packet.sequence;
      out.resetTiming = true;
      out.event = packet.type == HandTest::Gesture;
      return out;
    }
    const uint32_t difference = packet.sequence - sequence_;
    if (difference == 0 || difference >= 0x80000000UL) return out;
    sequence_ = packet.sequence;
    out.event = packet.type == HandTest::Gesture;
    out.missed = out.event ? difference - 1 : difference;
    out.resetTiming = out.missed != 0;
    return out;
  }
 private:
  bool seen_ = false;
  uint32_t session_ = 0;
  uint32_t sequence_ = 0;
};
