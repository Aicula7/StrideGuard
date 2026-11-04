#ifndef PACKET_DEF_H
#define PACKET_DEF_H

#include <stdint.h>

// Service and Characteristic UUIDs for Foot Pod
#define FOOT_SERVICE_UUID        "799B0049-56B0-FE94-9DAA-8DC669EFFDE7"
#define FOOT_CHARACTERISTIC_UUID "0x12345678-1234-1234-1234-1234567890AB"

// Service and Characteristic UUIDs for Wrist Unit (to Phone)
#define WRIST_SERVICE_UUID       "0x87654321-4321-4321-4321-BA0987654321"
#define WRIST_DATA_CHAR_UUID     "0xFEDCBAFE-4321-4321-4321-FEDCBAFEDCBA"

// Enforce 1-byte struct alignment across RISC-V and Xtensa architectures
#pragma pack(push, 1)
struct ImuEventPacket {
  int16_t ax;    // Accel X * 100 (m/s^2)
  int16_t ay;    // Accel Y * 100 (m/s^2)
  int16_t az;    // Accel Z * 100 (m/s^2)
  int16_t gx;    // Gyro X * 100 (rad/s or dps)
  int16_t gy;    // Gyro Y * 100 (rad/s or dps)
  int16_t gz;    // Gyro Z * 100 (rad/s or dps)
  int16_t temp;  // Temperature * 100 (°C)
  uint16_t flags;// Bitmask: Bit 0 = Alert flag, Bit 1 = Stance, Bit 2 = Heel strike
};
#pragma pack(pop)

#endif
