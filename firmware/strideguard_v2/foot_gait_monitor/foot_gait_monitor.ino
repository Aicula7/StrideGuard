#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <BLEDevice.h>
#include <BLE2902.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>
#include "GaitEventProtocol.h"
#include "GaitMonitor.h"
#include "WalkingConfig.h"

Adafruit_LSM6DS3TRC imu;
GaitMonitor::Detector detector;
BLECharacteristic* events = nullptr;
QueueHandle_t priorityQueue = nullptr, rawQueue = nullptr, commandQueue = nullptr;
std::atomic<bool> connected(false), restartAdvertising(false);
std::atomic<uint32_t> txDrops(0);
uint32_t bootSession=0, eventSequence=0, sampleGaps=0, badSamples=0;
uint32_t lastSampleMs=0, lastRawMs=0, lastStatusMs=0, settleStartMs=0;
char rollAxis=DefaultRollAxis;
bool rollConfirmed=DefaultRollAxisConfirmed, monitoring=true, settled=false;
bool wasConnected=false;
static_assert(DefaultRollAxis=='x' || DefaultRollAxis=='y' || DefaultRollAxis=='z', "Invalid default axis");

uint16_t stateFlags() {
  return (monitoring ? GaitEvents::Running : 0) |
      (detector.calibrated() ? GaitEvents::Ready : 0) |
      (rollConfirmed ? GaitEvents::AxisConfirmed : 0) |
      (detector.profile()==GaitMonitor::Profile::Handheld ? GaitEvents::Handheld : 0) |
      (!settled ? GaitEvents::Waiting : 0) |
      (rollAxis=='y' ? GaitEvents::AxisY : rollAxis=='z' ? GaitEvents::AxisZ : 0);
}
void enqueue(const GaitEvents::Packet& packet, bool raw=false) {
  if (!connected.load()) return;
  if (xQueueSend(raw ? rawQueue : priorityQueue, &packet, 0)!=pdTRUE) txDrops.fetch_add(1);
}
void publishStatus() {
  auto p=GaitEvents::make(GaitEvents::Status,millis());
  p.data.status={bootSession,stateFlags(),GaitEvents::u16(sampleGaps),
      GaitEvents::u16(badSamples),GaitEvents::u16(txDrops.load())};
  enqueue(p);
}
void publishBaseline() {
  if (!detector.calibrated()) return;
  auto p=GaitEvents::make(GaitEvents::Baseline,millis());
  p.data.baseline={bootSession,GaitEvents::u16(detector.baselineStrideMs()),
      GaitEvents::u16(detector.strideToleranceMs()),GaitEvents::u16(detector.impactThresholdG()*100),stateFlags()};
  enqueue(p);
}
// BLE notification work runs separately, so a slow radio cannot block the
// detector's approximately 100 Hz acquisition loop. Prefer events over raw data.
void transmitTask(void*) {
  GaitEvents::Packet p;
  for (;;) {
    if (connected.load() &&
        (xQueueReceive(priorityQueue,&p,0)==pdTRUE || xQueueReceive(rawQueue,&p,0)==pdTRUE)) {
      events->setValue(reinterpret_cast<uint8_t*>(&p),sizeof(p));
      events->notify(); // Notifications are not end-to-end acknowledged.
    }
    vTaskDelay(pdMS_TO_TICKS(5)>0 ? pdMS_TO_TICKS(5) : 1);
  }
}
class FootCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { connected.store(true); }
  void onDisconnect(BLEServer*) override {
    connected.store(false); restartAdvertising.store(true);
  }
};
class ControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    auto value=characteristic->getValue();
    for (size_t i=0;i<value.length();++i) {
      const char c=value[i];
      if (xQueueSend(commandQueue,&c,0)!=pdTRUE) txDrops.fetch_add(1);
    }
  }
};
void handleCommand(char c) {
  if (c=='\r' || c=='\n' || c==' ') return;
  bool accepted=true;
  switch(c) {
    case 'x': case 'y': case 'z':
      rollAxis=c; rollConfirmed=true; detector.reset(); break;
    case 'r': detector.reset(); break;
    case 'g': detector.interrupt(); monitoring=true; break;
    case 'p': detector.interrupt(); monitoring=false; break;
    case 'w': detector.setProfile(GaitMonitor::Profile::Walking); break;
    case 'd': detector.setProfile(GaitMonitor::Profile::Handheld); break;
    case 'n': rollConfirmed=false; detector.reset(); break;
    case 's': break;
    default: accepted=false;
  }
  auto p=GaitEvents::make(GaitEvents::Ack,millis());
  p.data.ack={bootSession,static_cast<uint8_t>(c),static_cast<uint8_t>(accepted),
      stateFlags(),detector.baselineCount(),detector.settings().baselineTarget,0};
  enqueue(p); publishStatus(); publishBaseline();
  Serial.printf("# CONTROL command=%c accepted=%d axis=%c roll_enabled=%d\n",c,accepted,rollAxis,rollConfirmed);
}
void sendAlert(GaitEvents::Kind kind,uint32_t time,uint16_t primary,int16_t secondary) {
  auto p=GaitEvents::make(kind,time);
  p.data.alert={bootSession,++eventSequence,primary,secondary};
  enqueue(p);
}
void setup() {
  Serial.begin(115200); // Do not wait for USB: battery boot must work.
  Wire.begin();
  if (!imu.begin_I2C(0x6A,&Wire)) {
    for (;;) { Serial.println("# FATAL FOOT_IMU_UNAVAILABLE address=0x6A"); delay(1000); }
  }
  imu.setAccelRange(LSM6DS_ACCEL_RANGE_8_G);
  imu.setGyroRange(LSM6DS_GYRO_RANGE_2000_DPS);
  imu.setAccelDataRate(LSM6DS_RATE_104_HZ);
  imu.setGyroDataRate(LSM6DS_RATE_104_HZ);
  priorityQueue=xQueueCreate(32,sizeof(GaitEvents::Packet));
  rawQueue=xQueueCreate(8,sizeof(GaitEvents::Packet));
  commandQueue=xQueueCreate(16,sizeof(char));
  if (!priorityQueue || !rawQueue || !commandQueue) {
    Serial.println("# FATAL QUEUE_ALLOCATION_FAILED"); for (;;) delay(1000);
  }
  bootSession=esp_random();
  BLEDevice::init("StrideGuard_Foot_Gait_V2");
  auto* server=BLEDevice::createServer(); server->setCallbacks(new FootCallbacks());
  auto* service=server->createService(GaitEvents::ServiceUuid);
  events=service->createCharacteristic(GaitEvents::CharacteristicUuid,BLECharacteristic::PROPERTY_NOTIFY);
  events->addDescriptor(new BLE2902());
  auto* control=service->createCharacteristic(GaitEvents::ControlUuid,BLECharacteristic::PROPERTY_WRITE);
  control->setCallbacks(new ControlCallbacks());
  service->start();
  BLEDevice::getAdvertising()->addServiceUUID(GaitEvents::ServiceUuid);
  BLEDevice::startAdvertising();
  if (xTaskCreate(transmitTask,"gait_tx",4096,nullptr,1,nullptr)!=pdPASS) {
    Serial.println("# FATAL TX_TASK_FAILED"); for (;;) delay(1000);
  }
  Serial.println("# READY automatic walking mode; waiting for wrist receiver. USB commands also accepted.");
}
void loop() {
  if (restartAdvertising.exchange(false)) BLEDevice::startAdvertising();
  const bool linked=connected.load();
  if (linked!=wasConnected) {
    wasConnected=linked;
    detector.interrupt();
    if (linked) {
      settleStartMs=millis(); settled=false;
      Serial.println("# LINK connected; stand still for 3 seconds.");
    } else {
      xQueueReset(rawQueue); xQueueReset(priorityQueue); xQueueReset(commandQueue);
      settled=false;
      Serial.println("# LINK disconnected; acquisition paused, baseline retained until power loss/reset.");
    }
  }
  while (Serial.available()) handleCommand(static_cast<char>(Serial.read()));
  char c;
  while (xQueueReceive(commandQueue,&c,0)==pdTRUE) handleCommand(c);
  const uint32_t now=millis();
  if (!linked) { delay(1); return; }
  if (!settled && now-settleStartMs>=StartupSettleMs) {
    settled=true; detector.interrupt(); publishStatus();
  }
  if (now-lastStatusMs>=1000) {
    lastStatusMs=now; publishStatus(); publishBaseline();
  }
  if (now-lastSampleMs<SamplePeriodMs) { delay(1); return; }
  lastSampleMs=now;
  sensors_event_t acceleration,angularRate,temperature;
  if (!imu.getEvent(&acceleration,&angularRate,&temperature)) {
    ++badSamples; detector.interrupt(); return;
  }
  const auto& a=acceleration.acceleration;
  const auto& w=angularRate.gyro;
  if (!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z)||
      !std::isfinite(w.x)||!std::isfinite(w.y)||!std::isfinite(w.z)) {
    ++badSamples; detector.interrupt(); return;
  }
  constexpr float DegPerRad=57.29578f;
  if (now-lastRawMs>=RawPeriodMs) {
    lastRawMs=now;
    auto p=GaitEvents::make(GaitEvents::Raw,now);
    p.data.raw={GaitEvents::i16(a.x/GaitMonitor::Gravity*1000),GaitEvents::i16(a.y/GaitMonitor::Gravity*1000),
        GaitEvents::i16(a.z/GaitMonitor::Gravity*1000),GaitEvents::i16(w.x*DegPerRad*10),
        GaitEvents::i16(w.y*DegPerRad*10),GaitEvents::i16(w.z*DegPerRad*10)};
    enqueue(p,true);
  }
  if (!monitoring || !settled) return;
  const float rollDps=(rollAxis=='x' ? w.x : rollAxis=='y' ? w.y : w.z)*DegPerRad;
  const float magnitude=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z)/GaitMonitor::Gravity;
  const bool clipped=std::max({std::fabs(a.x),std::fabs(a.y),std::fabs(a.z)})>=7.84f*GaitMonitor::Gravity ||
      std::max({std::fabs(w.x),std::fabs(w.y),std::fabs(w.z)})*DegPerRad>=1960;
  const auto result=detector.observe(now,magnitude,rollDps,clipped,rollConfirmed);
  if (result.gap) ++sampleGaps;
  if (!result.contact) return;
  // Queue alerts first so raw traffic cannot hold up haptic notification.
  if (result.suddenRoll) sendAlert(GaitEvents::SuddenRoll,result.contactMs,
      GaitEvents::u16(result.rollPeakDps),GaitEvents::i16(result.rollAngleDeg*10));
  if (result.hardLanding) sendAlert(GaitEvents::HardLanding,result.contactMs,
      GaitEvents::u16(result.peakImpactG*100),GaitEvents::i16(detector.impactThresholdG()*100));
  if (result.timingAlert) sendAlert(GaitEvents::Timing,result.contactMs,
      result.strideMs,GaitEvents::i16(detector.baselineStrideMs()));
  uint16_t flags=(result.clipped?GaitEvents::Clipped:0) |
      (result.suddenRoll?GaitEvents::RollAlert:0) | (result.hardLanding?GaitEvents::ImpactAlert:0) |
      (result.timingAlert?GaitEvents::TimingAlert:0) | (result.intervalValid?GaitEvents::ValidInterval:0) |
      (result.baselineAccepted?GaitEvents::BaselineAccepted:0) | (detector.calibrated()?GaitEvents::Calibrated:0) |
      (result.strideOutlier?GaitEvents::TimingOutlier:0) | (rollConfirmed?GaitEvents::RollEnabled:0) |
      (result.timingActive?GaitEvents::TimingActive:0);
  auto p=GaitEvents::make(GaitEvents::Contact,result.contactMs);
  p.data.contact={result.strideMs,GaitEvents::u16(result.peakImpactG*100),GaitEvents::u16(result.rollPeakDps),
      GaitEvents::i16(result.rollAngleDeg*10),flags,detector.baselineCount(),
      static_cast<uint8_t>(result.timingWindowCount | (result.timingOutliers<<4))};
  enqueue(p);
  if (result.baselineReady) publishBaseline();
}
