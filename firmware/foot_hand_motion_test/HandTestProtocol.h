#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Both ESP32 nodes use this exact versioned 20-byte little-endian layout.
namespace HandTest {
static constexpr uint16_t Magic = 0x5347;
static constexpr uint8_t Version = 1;
static constexpr uint8_t Gesture = 1;
static constexpr uint8_t Heartbeat = 2;
static constexpr const char* ServiceUuid = "7dfed001-2b6a-4db6-ae70-8dc32f7e5900";
static constexpr const char* CharacteristicUuid = "7dfed002-2b6a-4db6-ae70-8dc32f7e5900";
struct Packet {
  uint16_t magic;
  uint8_t version;
  uint8_t type;
  uint32_t bootSession;
  uint32_t sequence;
  uint32_t timestampMs;
  int16_t accelerationDeviationCenti;
  uint16_t angularRateCenti;
};
static_assert(sizeof(Packet) == 20, "Hand-test packet must fit the default BLE payload");
inline bool decode(const uint8_t* data, size_t length, Packet& packet) {
  if (!data || length != sizeof(Packet)) return false;
  memcpy(&packet, data, sizeof(Packet));
  return packet.magic == Magic && packet.version == Version &&
         (packet.type == Gesture || packet.type == Heartbeat);
}
}
