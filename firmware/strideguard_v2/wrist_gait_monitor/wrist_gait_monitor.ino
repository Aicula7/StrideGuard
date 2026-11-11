#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_DRV2605.h>
#include <BLEDevice.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include "GaitEventProtocol.h"
#if defined(CONFIG_BLUEDROID_ENABLED)
#include <esp_gattc_api.h>
#endif

Adafruit_DRV2605 drv;
bool hapticReady=false;
uint8_t hapticPhase=0,hapticPhases=0,hapticPriority=0;
uint32_t phaseStartMs=0;
constexpr uint32_t PulseMs=110;
constexpr uint8_t DriveLevel=0x40;
BLEClient* client=nullptr;
BLEScan* scanner=nullptr;
BLERemoteCharacteristic* control=nullptr;
QueueHandle_t eventQueue=nullptr,rawQueue=nullptr;
std::atomic<bool> disconnected(false);
std::atomic<BLEAdvertisedDevice*> discovered(nullptr);
std::atomic<uint32_t> rawDrops(0),eventDrops(0),invalidPackets(0);
bool subscribed=false,showRaw=true,seenSession=false,seenAlert=false,seenRaw=false,seenBaseline=false;
bool sessionVerified=false;
constexpr uint32_t SessionTimeoutMs=6000;
uint32_t subscriptionStartedMs=0;
#if defined(CONFIG_BLUEDROID_ENABLED)
esp_bd_addr_t refreshAddress={};
bool refreshPending=false,refreshAttempted=false;
#endif
uint32_t bootSession=0,lastSequence=0,lastRawMs=0,lastPacketAt=0,lastSummaryMs=0;
uint16_t lastState=0,lastStride=0,lastTolerance=0,lastImpact=0;
uint32_t previousGaps=0,previousBad=0,previousTxDrops=0;

// Phone-facing UUIDs from Ali's App integration branch.
constexpr const char* PhoneServiceUuid="87654321-4321-4321-4321-BA0987654321";
constexpr const char* PhoneNotifyUuid="FEDCBAFE-4321-4321-4321-FEDCBAFEDCBA";
BLECharacteristic* phoneNotify=nullptr;
BLE2902* phoneCccd=nullptr;
std::atomic<bool> restartPhoneAdvertising(false);
bool phoneNotifyReported=false;

class PhoneServerCallbacks : public BLEServerCallbacks {
  void onDisconnect(BLEServer*) override { restartPhoneAdvertising.store(true); }
};

void startPhoneService() {
  auto* phoneServer=BLEDevice::createServer();
  phoneServer->setCallbacks(new PhoneServerCallbacks());
  auto* service=phoneServer->createService(PhoneServiceUuid);
  phoneNotify=service->createCharacteristic(PhoneNotifyUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  phoneCccd=new BLE2902();
  phoneNotify->addDescriptor(phoneCccd);
  service->start();
  auto* advertising=BLEDevice::getAdvertising();
  advertising->addServiceUUID(PhoneServiceUuid);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.printf("# PHONE ADVERTISING service=%s notify=%s\n",PhoneServiceUuid,PhoneNotifyUuid);
}

void forwardToPhone(GaitEvents::Packet& packet) {
  // Called on the main loop, after foot packet validation and local haptics.
  // Forward session events only. USB retains the full raw IMU stream.
  if (!sessionVerified || packet.kind==GaitEvents::Raw || !phoneNotify) return;
  phoneNotify->setValue(reinterpret_cast<uint8_t*>(&packet),sizeof(packet));
  if (!phoneCccd->getNotifications()) return;
  phoneNotify->notify();
  if (!phoneNotifyReported) {
    phoneNotifyReported=true;
    Serial.printf("# PHONE NOTIFY_REQUESTED bytes=%u kind=%u\n",static_cast<unsigned>(sizeof(packet)),packet.kind);
  }
}

void stopPattern() {
  if (hapticReady) drv.setRealtimeValue(0);
  hapticPhase=hapticPhases=hapticPriority=0;
}
bool startPattern(uint8_t pulses) {
  if (!hapticReady || (hapticPhases && pulses<hapticPriority)) return false;
  stopPattern(); hapticPriority=pulses; hapticPhases=pulses*2-1;
  phaseStartMs=millis(); drv.setRealtimeValue(DriveLevel);
  return true;
}
void updatePattern() {
  if (!hapticPhases || millis()-phaseStartMs<PulseMs) return;
  if (++hapticPhase==hapticPhases) { stopPattern(); return; }
  phaseStartMs=millis(); drv.setRealtimeValue(hapticPhase%2==0 ? DriveLevel : 0);
}
void printHelp() {
  Serial.println("# HELP Protocol V2 | foot detector ~100 Hz | wireless raw trace ~50 Hz | baud=115200");
  Serial.println("# HELP PC commands: r=recalibrate; p=pause; g=resume; w=walking/reset; d=handheld/reset");
  Serial.println("# HELP x/y/z=confirm SENSOR roll axis and RESET baseline; n=disable roll/reset; s=status");
  Serial.println("# HELP Axis must point along shoe heel-to-toe. Roll detection OFF until confirmed; timing/impact still work.");
  Serial.println("# HELP t=toggle raw rows on PC (radio continues); v=test 3-pulse motor pattern; h=help");
  Serial.println("# HELP ALERT TIMING_DEVIATION=1 pulse; HARD_LANDING=2 pulses; SUSPECTED_SUDDEN_ROLL=3 pulses");
  Serial.println("# HELP CALIBRATING=learning baseline; WITHIN_BASELINE=within tested timing/impact limits, not a medical judgement");
  Serial.println("# HELP Single-foot interval: same foot contact to next contact, NOT left+right step count.");
  Serial.println("# HELP Stopping/very long intervals may be rejected, rather than generating timing alerts.");
  Serial.println("# HELP Raw acceleration includes gravity; acquisition timestamps are FOOT milliseconds since boot.");
  Serial.println("# COLUMNS DATA,foot_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps");
}
void receivePacket(BLERemoteCharacteristic*,uint8_t* data,size_t size,bool) {
  GaitEvents::Packet p;
  if (!GaitEvents::decode(data,size,p)) { invalidPackets.fetch_add(1); return; }
  if (p.kind==GaitEvents::Raw) {
    if (xQueueSend(rawQueue,&p,0)!=pdTRUE) rawDrops.fetch_add(1);
  } else if (xQueueSend(eventQueue,&p,0)!=pdTRUE) eventDrops.fetch_add(1);
}
class ClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient*) override {}
  void onDisconnect(BLEClient*) override { disconnected.store(true); }
};
class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) override {
    if (!device.haveServiceUUID() || !device.isAdvertisingService(BLEUUID(GaitEvents::ServiceUuid))) return;
    auto* target=new BLEAdvertisedDevice(device);
    BLEAdvertisedDevice* expected=nullptr;
    if (discovered.compare_exchange_strong(expected,target)) scanner->stop();
    else delete target;
  }
};
bool scheduleGattRefresh() {
#if defined(CONFIG_BLUEDROID_ENABLED)
  if (refreshAttempted) return false;
  auto address=client->getPeerAddress();
  memcpy(refreshAddress,address.getNative(),sizeof(refreshAddress));
  refreshAttempted=true;
  refreshPending=true;
  return true;
#else
  return false;
#endif
}

void connectFoot() {
  scanner->start(2,false);
  auto* target=discovered.exchange(nullptr);
  if (target && client->connect(target)) {
    // The same client can retain a service map after disconnect. Rediscover
    // it on every link before checking the current foot's characteristics.
    client->getServices();
    auto* service=client->getService(BLEUUID(GaitEvents::ServiceUuid));
    auto* notify=service ? service->getCharacteristic(BLEUUID(GaitEvents::CharacteristicUuid)) : nullptr;
    auto* write=service ? service->getCharacteristic(BLEUUID(GaitEvents::ControlUuid)) : nullptr;
    const bool notifyReady=notify && notify->canNotify();
    const bool writeReady=write && write->canWrite();
    Serial.printf("# LINK GATT_CHECK service=%s data=%s control=%s\n",
        service?"FOUND":"MISSING",notifyReady?"READY":notify?"NO_NOTIFY":"MISSING",
        writeReady?"READY":write?"NOT_WRITABLE":"MISSING");
    // Refresh stale GATT handles once before accepting a read-only session.
    const bool retryDiscovery=(!notifyReady || !writeReady) && scheduleGattRefresh();
    if (notifyReady && !retryDiscovery && !disconnected.load()) {
      control=writeReady ? write : nullptr;
      xQueueReset(rawQueue); xQueueReset(eventQueue);
      seenRaw=false; sessionVerified=false;
      notify->registerForNotify(receivePacket);
      subscribed=true;
      subscriptionStartedMs=lastPacketAt=millis();
      Serial.println("# LINK WAITING_FOR_V2_DATA");
    } else {
      control=nullptr;
      if (retryDiscovery) Serial.println("# LINK RETRYING_AFTER_GATT_REFRESH");
      else Serial.println("# LINK DATA_CHARACTERISTIC_UNAVAILABLE");
      if (client->isConnected()) client->disconnect();
    }
  }
  delete target; scanner->clearResults();
}
void observeSession(uint32_t session) {
  if (!sessionVerified) {
    Serial.printf("# LINK FOOT_CONNECTED protocol=V2 control=%s | wait for acquisition=RUNNING\n",
        control?"READY":"USB_ONLY");
#if defined(CONFIG_BLUEDROID_ENABLED)
    if (control) refreshAttempted=false;
#endif
  }
  sessionVerified=true;
  if (!seenSession || bootSession!=session) {
    seenSession=true; bootSession=session; seenAlert=false; seenRaw=false; seenBaseline=false;
    previousGaps=previousBad=previousTxDrops=0;
    Serial.printf("# SESSION foot_boot=%lu | NEW_FOOT_BOOT baseline/settings reset to configured defaults\n",
        static_cast<unsigned long>(session));
  }
}
void printState(uint16_t state,uint32_t time) {
  lastState=state;
  Serial.printf("# STATUS foot_ms=%lu mode=%s acquisition=%s baseline=%s roll_axis=%c roll_detection=%s\n",
      static_cast<unsigned long>(time),state&GaitEvents::Handheld ? "HANDHELD" : "WALKING",
      state&GaitEvents::Waiting ? "WAITING_SETTLE" : state&GaitEvents::Running ? "RUNNING" : "PAUSED",
      state&GaitEvents::Ready ? "READY" : "CALIBRATING",GaitEvents::axis(state),
      state&GaitEvents::AxisConfirmed ? "ENABLED" : "DISABLED_AXIS_UNCONFIRMED");
}
void processPacket(const GaitEvents::Packet& p) {
  lastPacketAt=millis();
  switch(p.kind) {
    case GaitEvents::Raw: {
      // Do not label data with a previous boot's session during reconnection.
      // Wait for a status/baseline/ack/alert that identifies this connection.
      if (!sessionVerified) break;
      if (seenRaw) {
        const uint32_t dt=p.timestampMs-lastRawMs;
        if (dt>35 && dt<0x80000000u) Serial.printf("# QUALITY RAW_STREAM_GAP foot_ms=%lu gap_ms=%lu expected_about_ms=20 | trace has missing/delayed samples\n",
            static_cast<unsigned long>(p.timestampMs),static_cast<unsigned long>(dt));
      }
      seenRaw=true; lastRawMs=p.timestampMs;
      if (showRaw) {
        const auto& r=p.data.raw;
        Serial.printf("DATA,%lu,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f\n",static_cast<unsigned long>(p.timestampMs),
            r.ax/1000.0f,r.ay/1000.0f,r.az/1000.0f,r.gx/10.0f,r.gy/10.0f,r.gz/10.0f);
      }
      break;
    }
    case GaitEvents::Status: {
      const auto& s=p.data.status;
      observeSession(s.bootSession); printState(s.state,p.timestampMs);
      if (s.sampleGaps!=previousGaps || s.badSamples!=previousBad || s.txDrops!=previousTxDrops) {
        Serial.printf("# QUALITY FOOT_COUNTERS sample_gaps=%u invalid_imu_samples=%u tx_queue_drops=%u | cumulative this boot; gaps interrupt pending timing evidence\n",
            s.sampleGaps,s.badSamples,s.txDrops);
        previousGaps=s.sampleGaps; previousBad=s.badSamples; previousTxDrops=s.txDrops;
      }
      break;
    }
    case GaitEvents::Baseline: {
      const auto& b=p.data.baseline;
      observeSession(b.bootSession);
      if (!seenBaseline || b.strideMs!=lastStride || b.toleranceMs!=lastTolerance || b.impactCentiG!=lastImpact) {
        seenBaseline=true; lastStride=b.strideMs; lastTolerance=b.toleranceMs; lastImpact=b.impactCentiG;
        Serial.printf("# BASELINE READY foot_ms=%lu stride_center_ms=%u timing_tolerance_ms=%u timing_range_ms=%u..%u hard_landing_threshold_g=%.2f roll_rate_threshold_dps=%u roll_angle_threshold_deg=%u\n",
            static_cast<unsigned long>(p.timestampMs),b.strideMs,b.toleranceMs,
            b.strideMs>b.toleranceMs ? b.strideMs-b.toleranceMs : 0,b.strideMs+b.toleranceMs,
            b.impactCentiG/100.0f,b.state&GaitEvents::Handheld ? 100u : 250u,
            b.state&GaitEvents::Handheld ? 10u : 12u);
      }
      break;
    }
    case GaitEvents::Contact: {
      if (!sessionVerified) break;
      const auto& c=p.data.contact; const auto f=c.flags;
      const char* classification=f&GaitEvents::Clipped ? "SENSOR_CLIPPED_REVIEW_REQUIRED" :
          f&GaitEvents::RollAlert ? "SUSPECTED_SUDDEN_ROLL" :
          f&GaitEvents::ImpactAlert ? "HARD_LANDING" :
          f&GaitEvents::TimingAlert ? "TIMING_DEVIATION_TRIGGERED" :
          !(f&GaitEvents::Calibrated) ? "CALIBRATING" :
          !(f&GaitEvents::ValidInterval) ? "INTERVAL_UNAVAILABLE_OR_REJECTED" :
          f&GaitEvents::TimingActive ? "TIMING_ALERT_ACTIVE" :
          f&GaitEvents::TimingOutlier ? "TIMING_OUTLIER_NOT_YET_PERSISTENT" : "WITHIN_BASELINE";
      Serial.printf("# CONTACT foot_ms=%lu type=%s stride_ms=%u interval_valid=%u peak_g=%.2f roll_peak_dps=%u roll_angle_deg=%.1f baseline=%u/%u baseline_sample_accepted=%u timing_outliers=%u/%u roll_enabled=%u flags=0x%04X\n",
          static_cast<unsigned long>(p.timestampMs),classification,c.strideMs,(f&GaitEvents::ValidInterval)?1u:0u,
          c.peakCentiG/100.0f,c.rollDps,c.rollDeciDeg/10.0f,c.baselineCount,
          lastState&GaitEvents::Handheld ? 8u : 20u,(f&GaitEvents::BaselineAccepted)?1u:0u,
          c.timingWindow>>4,c.timingWindow&15,(f&GaitEvents::RollEnabled)?1u:0u,f);
      if (f&GaitEvents::Clipped) Serial.println("# QUALITY CONTACT_CLIPPED | sensor range reached; this contact excluded from baseline/timing/alerts");
      break;
    }
    case GaitEvents::Timing: case GaitEvents::HardLanding: case GaitEvents::SuddenRoll: {
      const auto& a=p.data.alert; observeSession(a.bootSession);
      if (seenAlert) {
        uint32_t advance=a.sequence-lastSequence;
        if (!advance || advance>=0x80000000u) return;
        if (advance>1) Serial.printf("# QUALITY MISSED_ALERTS count=%lu\n",static_cast<unsigned long>(advance-1));
      } else if (a.sequence>1) {
        Serial.printf("# QUALITY EARLIER_ALERTS_NOT_RECEIVED count=%lu\n",static_cast<unsigned long>(a.sequence-1));
      }
      seenAlert=true; lastSequence=a.sequence;
      const char* haptic=hapticReady ? "AVAILABLE" : "UNAVAILABLE_SERIAL_ONLY";
      if (p.kind==GaitEvents::Timing) {
        const bool played=startPattern(1);
        Serial.printf("# ALERT type=TIMING_DEVIATION foot_ms=%lu stride_ms=%u baseline_ms=%d rule=latest_outlier_AND_at_least_3_of_last_5_outliers requested_vibration_pulses=1 motor_action=%s haptic=%s\n",
            static_cast<unsigned long>(p.timestampMs),a.primary,a.secondary,played?"STARTED":"SUPPRESSED_OR_UNAVAILABLE",haptic);
      } else if (p.kind==GaitEvents::HardLanding) {
        const bool played=startPattern(2);
        Serial.printf("# ALERT type=HARD_LANDING foot_ms=%lu impact_peak_g=%.2f threshold_g=%.2f rule=peak_at_or_above_personal_threshold requested_vibration_pulses=2 motor_action=%s haptic=%s\n",
            static_cast<unsigned long>(p.timestampMs),a.primary/100.0f,a.secondary/100.0f,played?"STARTED":"SUPPRESSED_OR_UNAVAILABLE",haptic);
      } else {
        const bool played=startPattern(3);
        Serial.printf("# ALERT type=SUSPECTED_SUDDEN_ROLL foot_ms=%lu roll_peak_dps=%u signed_angle_deg=%.1f rule=contact_window_rotation_rate_AND_angle requested_vibration_pulses=3 motor_action=%s haptic=%s | motion suspicion, not confirmed ankle injury\n",
            static_cast<unsigned long>(p.timestampMs),a.primary,a.secondary/10.0f,played?"STARTED":"SUPPRESSED_OR_UNAVAILABLE",haptic);
      }
      break;
    }
    case GaitEvents::Ack: {
      const auto& a=p.data.ack; observeSession(a.bootSession);
      if (a.accepted && (a.command=='r'||a.command=='w'||a.command=='d'||a.command=='x'||a.command=='y'||a.command=='z'||a.command=='n')) {
        seenBaseline=false; lastStride=lastTolerance=lastImpact=0;
      }
      Serial.printf("# CONTROL ACK command=%c accepted=%u baseline=%u/%u foot_ms=%lu\n",
          a.command,a.accepted,a.baselineCount,a.baselineTarget,static_cast<unsigned long>(p.timestampMs));
      printState(a.state,p.timestampMs);
      break;
    }
  }
}
void handlePcCommand(char c) {
  if (c=='\r'||c=='\n'||c==' ') return;
  if (c=='h') { printHelp(); return; }
  if (c=='t') { showRaw=!showRaw; Serial.printf("# CONTROL RAW_PC_OUTPUT=%s\n",showRaw?"ON":"OFF"); return; }
  if (c=='v') {
    const bool started=startPattern(3);
    Serial.printf("# CONTROL MOTOR_TEST requested_pulses=3 motor_action=%s\n",started?"STARTED":"UNAVAILABLE");
    return;
  }
  if (!strchr("rpgwdxyzns",c)) { Serial.println("# CONTROL UNKNOWN_COMMAND send h for help"); return; }
  if (!subscribed || !sessionVerified) {
    Serial.println("# CONTROL NOT_SENT reason=WAIT_FOR_VALID_FOOT_DATA"); return;
  }
  if (!control) {
    Serial.println("# CONTROL NOT_SENT reason=BLE_CONTROL_UNAVAILABLE | use foot USB serial for settings"); return;
  }
  uint8_t value=static_cast<uint8_t>(c);
  control->writeValue(&value,1,true);
  Serial.printf("# CONTROL SENT command=%c | wait for CONTROL ACK to confirm foot applied it\n",c);
}
void setup() {
  Serial.begin(115200); Wire.begin();
  hapticReady=drv.begin(&Wire);
  if (hapticReady) { drv.setMode(DRV2605_MODE_REALTIME); stopPattern(); }
  else Serial.println("# QUALITY HAPTIC_UNAVAILABLE | data recording and alerts still available via USB");
  eventQueue=xQueueCreate(32,sizeof(GaitEvents::Packet));
  rawQueue=xQueueCreate(64,sizeof(GaitEvents::Packet));
  if (!eventQueue||!rawQueue) { Serial.println("# FATAL RX_QUEUE_FAILED"); for (;;) delay(1000); }
  BLEDevice::init("StrideGuard_Wrist_Gait_V2");
  client=BLEDevice::createClient(); client->setClientCallbacks(new ClientCallbacks());
  scanner=BLEDevice::getScan(); scanner->setAdvertisedDeviceCallbacks(new ScanCallbacks()); scanner->setActiveScan(true);
  startPhoneService();
  printHelp(); Serial.println("# LINK WRIST_READY build=V2_PHONE_BRIDGE searching for V2 foot");
}
void loop() {
  if (restartPhoneAdvertising.exchange(false)) {
    phoneCccd->setNotifications(false);
    phoneNotifyReported=false;
    BLEDevice::startAdvertising();
    Serial.println("# PHONE ADVERTISING restarted");
  }
  if (disconnected.exchange(false)) {
    subscribed=false; control=nullptr; xQueueReset(eventQueue); xQueueReset(rawQueue); stopPattern();
    seenRaw=false; sessionVerified=false;
#if defined(CONFIG_BLUEDROID_ENABLED)
    if (refreshPending) {
      refreshPending=false;
      const auto result=esp_ble_gattc_cache_refresh(refreshAddress);
      Serial.printf("# LINK GATT_CACHE_REFRESH result=%s code=%d\n",result==ESP_OK?"QUEUED":"FAILED",static_cast<int>(result));
    }
#endif
    Serial.println("# LINK FOOT_DISCONNECTED | no live data; reconnecting automatically");
  }
  while (Serial.available()) handlePcCommand(static_cast<char>(Serial.read()));
  updatePattern();
  if (!subscribed) {
    // A scan can block for two seconds. Finish a local motor test first.
    if (!hapticPhases) connectFoot();
    delay(1); return;
  }
  GaitEvents::Packet p;
  for (int i=0;i<8 && xQueueReceive(eventQueue,&p,0)==pdTRUE;++i) {
    processPacket(p);
    forwardToPhone(p);
  }
  for (int i=0;i<4 && xQueueReceive(rawQueue,&p,0)==pdTRUE;++i) processPacket(p);
  if (!sessionVerified && millis()-subscriptionStartedMs>=SessionTimeoutMs) {
    Serial.println("# LINK NO_VALID_V2_SESSION | check foot data notifications and V2 firmware");
    scheduleGattRefresh();
    if (client->isConnected()) client->disconnect();
    return;
  }
  if (millis()-lastSummaryMs>=2000) {
    lastSummaryMs=millis();
    const uint32_t rd=rawDrops.exchange(0),ed=eventDrops.exchange(0),invalid=invalidPackets.exchange(0);
    if (rd||ed||invalid) Serial.printf("# QUALITY WRIST_DROPS last_period_raw=%lu events=%lu invalid_packets=%lu | lost records cannot be reconstructed\n",
        static_cast<unsigned long>(rd),static_cast<unsigned long>(ed),static_cast<unsigned long>(invalid));
    if (millis()-lastPacketAt>3000) Serial.println("# QUALITY NO_FOOT_DATA_OVER_3_SECONDS | check foot power/IMU/radio; absence of alerts does not confirm normal walking");
  }
  delay(1);
}
