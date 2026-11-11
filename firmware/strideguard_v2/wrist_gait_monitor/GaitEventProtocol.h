#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <cmath>

// Version 2: BOTH boards must use this exact header. Every notification fits
// the default 20-byte BLE payload; no MTU negotiation is required.
namespace GaitEvents {
constexpr uint16_t Magic = 0x5347;
constexpr uint8_t Version = 2;
constexpr const char* ServiceUuid = "7dfed011-2b6a-4db6-ae70-8dc32f7e5900";
constexpr const char* CharacteristicUuid = "7dfed012-2b6a-4db6-ae70-8dc32f7e5900";
constexpr const char* ControlUuid = "7dfed013-2b6a-4db6-ae70-8dc32f7e5900";
enum Kind : uint8_t { Raw = 1, Contact, Baseline, Status, Timing, HardLanding, SuddenRoll, Ack };
enum ContactFlag : uint16_t {
  Clipped=1, RollAlert=2, ImpactAlert=4, TimingAlert=8, ValidInterval=16,
  BaselineAccepted=32, Calibrated=64, TimingOutlier=128, RollEnabled=256,
  TimingActive=512
};
enum StateFlag : uint16_t {
  Running=1, Ready=2, AxisConfirmed=4, Handheld=8, Waiting=16,
  AxisY=32, AxisZ=64
};
#pragma pack(push, 1)
struct RawData { int16_t ax, ay, az, gx, gy, gz; }; // milli-g and 0.1 deg/s
struct ContactData {
  uint16_t strideMs, peakCentiG, rollDps;
  int16_t rollDeciDeg;
  uint16_t flags;
  uint8_t baselineCount, timingWindow; // count low 4 bits, outliers high 4 bits
};
struct BaselineData {
  uint32_t bootSession;
  uint16_t strideMs, toleranceMs, impactCentiG, state;
};
struct StatusData {
  uint32_t bootSession;
  uint16_t state, sampleGaps, badSamples, txDrops; // counters saturate at 65535
};
struct AlertData {
  uint32_t bootSession, sequence;
  uint16_t primary;
  int16_t secondary;
};
struct AckData {
  uint32_t bootSession;
  uint8_t command, accepted;
  uint16_t state;
  uint8_t baselineCount, baselineTarget;
  uint16_t reserved;
};
struct Packet {
  uint16_t magic;
  uint8_t version, kind;
  uint32_t timestampMs; // foot's acquisition/contact clock, NOT arrival time
  union Payload {
    RawData raw;
    ContactData contact;
    BaselineData baseline;
    StatusData status;
    AlertData alert;
    AckData ack;
  } data;
};
#pragma pack(pop)
static_assert(sizeof(Packet)==20, "BLE notification must be 20 bytes");
inline Packet make(Kind kind, uint32_t time) {
  Packet p = {}; p.magic=Magic; p.version=Version; p.kind=kind; p.timestampMs=time; return p;
}
inline bool decode(const uint8_t* bytes, size_t length, Packet& packet) {
  if (!bytes || length!=sizeof(Packet)) return false;
  memcpy(&packet,bytes,sizeof(packet));
  return packet.magic==Magic && packet.version==Version && packet.kind>=Raw && packet.kind<=Ack;
}
inline uint16_t u16(float value) {
  if (!std::isfinite(value) || value<=0) return 0;
  return value>=65535.0f ? 65535 : static_cast<uint16_t>(std::lround(value));
}
inline int16_t i16(float value) {
  if (!std::isfinite(value)) return 0;
  if (value<=-32768) return -32768;
  if (value>=32767) return 32767;
  return static_cast<int16_t>(std::lround(value));
}
inline char axis(uint16_t state) { return state&AxisY ? 'y' : state&AxisZ ? 'z' : 'x'; }
}
