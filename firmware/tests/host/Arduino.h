#pragma once
// Host-only adapters for exercising the actual wrist sketch's control flow.
// These model API behavior; they do not validate ESP32 BLE or physical I2C.
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

static uint32_t hostNow = 0;
inline uint32_t millis() { return hostNow; }
inline void delay(uint32_t ms) { hostNow += ms; }
struct HostSerial {
  std::deque<char> input;
  std::string output;
  void begin(unsigned) {}
  int available() const { return static_cast<int>(input.size()); }
  int read() { const char c = input.front(); input.pop_front(); return c; }
  void println(const char* text) { output += text; output += '\n'; }
  void printf(const char* format, ...) {
    char buffer[2048]; va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); output += buffer;
  }
} static Serial;
struct TwoWire { void begin() {} } static Wire;

#define DRV2605_MODE_INTTRIG 0
#define DRV2605_MODE_REALTIME 5
class Adafruit_DRV2605 {
 public:
  bool available = true;
  uint8_t mode = 0;
  uint8_t rtp = 0;
  bool effect = false;
  int clicks = 0;
  bool begin() { return available; }
  void useERM() {}
  void selectLibrary(uint8_t) {}
  void setMode(uint8_t value) { mode = value; }
  void setRealtimeValue(uint8_t value) { rtp = value; }
  void stop() { effect = false; }  // GO does not zero RTP.
  void setWaveform(uint8_t, uint8_t) {}
  void go() { effect = true; ++clicks; }
  bool outputActive() const { return mode == DRV2605_MODE_REALTIME ? rtp != 0 : effect; }
};

struct HostQueue {
  size_t capacity;
  size_t width;
  std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = HostQueue*;
static constexpr int pdTRUE = 1;
inline QueueHandle_t xQueueCreate(size_t capacity, size_t width) {
  return new HostQueue{capacity, width, {}};
}
inline int xQueueSend(QueueHandle_t queue, const void* item, int) {
  if (queue->items.size() == queue->capacity) return 0;
  const uint8_t* data = static_cast<const uint8_t*>(item);
  queue->items.emplace_back(data, data + queue->width); return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t queue, void* item, int) {
  if (queue->items.empty()) return 0;
  memcpy(item, queue->items.front().data(), queue->width);
  queue->items.pop_front(); return pdTRUE;
}
inline void xQueueReset(QueueHandle_t queue) { queue->items.clear(); }

class BLEUUID { public: explicit BLEUUID(const char*) {} };
class BLERemoteCharacteristic {
 public:
  bool canNotify() const { return true; }
  void registerForNotify(void (*)(BLERemoteCharacteristic*, uint8_t*, size_t, bool)) {}
};
class BLERemoteService {
  BLERemoteCharacteristic characteristic;
 public: BLERemoteCharacteristic* getCharacteristic(BLEUUID) { return &characteristic; }
};
class BLEClient;
class BLEClientCallbacks {
 public:
  virtual ~BLEClientCallbacks() {}
  virtual void onConnect(BLEClient*) {}
  virtual void onDisconnect(BLEClient*) {}
};
class BLEAdvertisedDevice {
 public:
  bool haveServiceUUID() const { return true; }
  bool isAdvertisingService(BLEUUID) const { return false; }
};
class BLEAdvertisedDeviceCallbacks {
 public:
  virtual ~BLEAdvertisedDeviceCallbacks() {}
  virtual void onResult(BLEAdvertisedDevice) {}
};
class BLEClient {
  BLEClientCallbacks* callbacks = nullptr;
  BLERemoteService service;
 public:
  void setClientCallbacks(BLEClientCallbacks* cb) { callbacks = cb; }
  bool connect(BLEAdvertisedDevice*) { if (callbacks) callbacks->onConnect(this); return true; }
  void disconnect() { if (callbacks) callbacks->onDisconnect(this); }
  BLERemoteService* getService(BLEUUID) { return &service; }
};
class BLEScan {
  BLEAdvertisedDeviceCallbacks* callbacks = nullptr;
 public:
  int starts = 0;
  void start(unsigned, bool) { ++starts; }
  void stop() {}
  void clearResults() {}
  void setActiveScan(bool) {}
  void setAdvertisedDeviceCallbacks(BLEAdvertisedDeviceCallbacks* value) { callbacks = value; }
};
class BLEDevice {
 public:
  static void init(const char*) {}
  static BLEClient* createClient() { return new BLEClient; }
  static BLEScan* getScan() { static BLEScan scanner; return &scanner; }
};
