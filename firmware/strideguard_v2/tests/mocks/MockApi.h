#pragma once
// Host-only API doubles. Never copy this directory into Arduino sketch folders.
// These check our C++ and control flow; they do NOT establish SDK compatibility.
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <string>
#include <deque>
#include <vector>
#include <cstdarg>
#include <cstdio>
inline uint32_t mockNow=0;
inline uint32_t millis(){return mockNow;}
inline void delay(uint32_t dt){mockNow+=dt;}
struct SerialApi {
 std::string output,input;
 void begin(int){}
 int available(){return input.size();}
 int read(){char c=input.front();input.erase(0,1);return c;}
 void println(const char* text){output+=text;output+='\n';}
 void printf(const char* fmt,...){char b[4096];va_list args;va_start(args,fmt);vsnprintf(b,sizeof(b),fmt,args);va_end(args);output+=b;}
};
inline SerialApi Serial;
struct WireApi{void begin(){}};
inline WireApi Wire;
struct sensors_event_t {struct Vector {float x=0,y=0,z=0;} acceleration,gyro;};
inline sensors_event_t mockAcceleration,mockAngularRate;
inline bool mockSensorOk=true;
struct Adafruit_LSM6DS3TRC {
 bool begin_I2C(int,WireApi*){return true;}
 void setAccelRange(int){} void setGyroRange(int){}
 void setAccelDataRate(int){} void setGyroDataRate(int){}
 bool getEvent(sensors_event_t* a,sensors_event_t* w,sensors_event_t*){*a=mockAcceleration;*w=mockAngularRate;return mockSensorOk;}
};
constexpr int LSM6DS_ACCEL_RANGE_8_G=8,LSM6DS_GYRO_RANGE_2000_DPS=2000,LSM6DS_RATE_104_HZ=104;
struct Adafruit_DRV2605 {bool begin(WireApi*){return true;}void setMode(int){}void setRealtimeValue(int){}};
constexpr int DRV2605_MODE_REALTIME=5;
struct MockQueue{size_t capacity,size;std::deque<std::vector<uint8_t>> items;};
using QueueHandle_t=MockQueue*;
constexpr int pdTRUE=1,pdPASS=1;
inline QueueHandle_t xQueueCreate(size_t n,size_t size){return new MockQueue{n,size,{}};}
inline int xQueueSend(QueueHandle_t q,const void* value,int){if(q->items.size()>=q->capacity)return 0;const auto* b=static_cast<const uint8_t*>(value);q->items.emplace_back(b,b+q->size);return 1;}
inline int xQueueReceive(QueueHandle_t q,void* value,int){if(q->items.empty())return 0;memcpy(value,q->items.front().data(),q->size);q->items.pop_front();return 1;}
inline void xQueueReset(QueueHandle_t q){q->items.clear();}
inline int xTaskCreate(void(*)(void*),const char*,int,void*,int,void*){return pdPASS;}
inline unsigned pdMS_TO_TICKS(unsigned n){return n;}
inline void vTaskDelay(unsigned){}
inline uint32_t esp_random(){return 123456789;}
class BLEUUID{public:explicit BLEUUID(const char* uuid):value(uuid){}std::string value;};
using esp_bd_addr_t=uint8_t[6];
constexpr int ESP_OK=0;
inline int mockGattRefreshes=0;
inline int esp_ble_gattc_cache_refresh(esp_bd_addr_t){++mockGattRefreshes;return ESP_OK;}
class BLEAddress{public:uint8_t* getNative(){return bytes;}private:uint8_t bytes[6]={};};
class BLEServer;
class BLECharacteristic;
class BLEClient;
class BLERemoteCharacteristic;
class BLEAdvertisedDevice;
class BLEServerCallbacks{public:virtual ~BLEServerCallbacks(){}virtual void onConnect(BLEServer*){}virtual void onDisconnect(BLEServer*){}};
class BLEClientCallbacks{public:virtual ~BLEClientCallbacks(){}virtual void onConnect(BLEClient*){}virtual void onDisconnect(BLEClient*){}};
class BLECharacteristicCallbacks{public:virtual ~BLECharacteristicCallbacks(){}virtual void onWrite(BLECharacteristic*){}};
class BLE2902{};
class BLECharacteristic {
public:
 static constexpr int PROPERTY_NOTIFY=1,PROPERTY_WRITE=2;
 std::string value;
 void setValue(uint8_t* data,size_t n){value.assign(reinterpret_cast<char*>(data),n);}
 std::string getValue(){return value;}
 void notify(){} void addDescriptor(BLE2902*){} void setCallbacks(BLECharacteristicCallbacks*){}
};
class BLEService{public:BLECharacteristic* createCharacteristic(const char*,int){return new BLECharacteristic;}void start(){}};
class BLEServer{public:void setCallbacks(BLEServerCallbacks*){}BLEService* createService(const char*){return new BLEService;}};
class BLEAdvertising{public:void addServiceUUID(const char*){}};
class BLEAdvertisedDevice{public:bool haveServiceUUID(){return true;}bool isAdvertisingService(BLEUUID){return true;}};
class BLEAdvertisedDeviceCallbacks{public:virtual ~BLEAdvertisedDeviceCallbacks(){}virtual void onResult(BLEAdvertisedDevice){}};
class BLEScan{public:void start(int,bool){}void stop(){}void clearResults(){}void setAdvertisedDeviceCallbacks(BLEAdvertisedDeviceCallbacks*){}void setActiveScan(bool){}};
class BLERemoteCharacteristic {
public:
 std::string written;
 bool canNotify(){return true;}bool canWrite(){return true;}
 void registerForNotify(void(*)(BLERemoteCharacteristic*,uint8_t*,size_t,bool)){}
 void writeValue(uint8_t* bytes,size_t n,bool){written.assign(reinterpret_cast<char*>(bytes),n);}
};
class BLERemoteService {
public:
 bool dataAvailable=true,controlAvailable=true;
 BLERemoteCharacteristic data,control;
 BLERemoteCharacteristic* getCharacteristic(BLEUUID uuid){
  if (uuid.value.find("7dfed012")==0) return dataAvailable?&data:nullptr;
  return controlAvailable?&control:nullptr;
 }
};
class BLEClient {
public:
 bool connected=false,serviceAvailable=true;
 int discoveries=0;
 BLERemoteService service;
 BLEClientCallbacks* callbacks=nullptr;
 bool connect(BLEAdvertisedDevice*){connected=true;return true;}
 bool isConnected(){return connected;}
 void disconnect(){connected=false;if(callbacks)callbacks->onDisconnect(this);}
 void setClientCallbacks(BLEClientCallbacks* cb){callbacks=cb;}
 BLEAddress getPeerAddress(){return BLEAddress();}
 void getServices(){++discoveries;}
 BLERemoteService* getService(BLEUUID){return serviceAvailable?&service:nullptr;}
};
class BLEDevice{public:static void init(const char*){}static BLEServer* createServer(){static BLEServer s;return &s;}static BLEAdvertising* getAdvertising(){static BLEAdvertising a;return &a;}static void startAdvertising(){}static BLEClient* createClient(){static BLEClient c;return &c;}static BLEScan* getScan(){static BLEScan s;return &s;}};
