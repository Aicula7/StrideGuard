#include <cassert>
#include <iostream>
#include "../wrist_gait_monitor/wrist_gait_monitor.ino"
int main(){
 setup();
 auto raw=GaitEvents::make(GaitEvents::Raw,100);raw.data.raw={1000,0,-1000,100,-200,0};
 Serial.output.clear();processPacket(raw);assert(Serial.output.empty()); // no old/unknown session attribution
 auto status=GaitEvents::make(GaitEvents::Status,100);status.data.status={42,GaitEvents::Running,0,0,0};
 processPacket(status);assert(sessionVerified&&bootSession==42);
 forwardToPhone(status);assert(phoneNotify->notifications==0);
 phoneCccd->setNotifications(true);
 forwardToPhone(status);assert(phoneNotify->notifications==1);
 GaitEvents::Packet phonePacket;
 assert(GaitEvents::decode(reinterpret_cast<const uint8_t*>(phoneNotify->value.data()),phoneNotify->value.size(),phonePacket));
 assert(phonePacket.kind==GaitEvents::Status && phonePacket.data.status.bootSession==42);
 forwardToPhone(raw);assert(phoneNotify->notifications==1);
 processPacket(raw);assert(Serial.output.find("DATA,100,1.000,0.000,-1.000,10.0,-20.0,0.0")!=std::string::npos);
 auto b=GaitEvents::make(GaitEvents::Baseline,200);b.data.baseline={42,1000,150,216,GaitEvents::Ready};
 processPacket(b);assert(Serial.output.find("timing_range_ms=850..1150")!=std::string::npos);
 for(auto kind:{GaitEvents::Timing,GaitEvents::HardLanding,GaitEvents::SuddenRoll}){
  auto p=GaitEvents::make(kind,300);p.data.alert={42,static_cast<uint32_t>(kind),300,216};processPacket(p);
 }
 assert(Serial.output.find("type=TIMING_DEVIATION")!=std::string::npos);
 assert(Serial.output.find("type=HARD_LANDING")!=std::string::npos);
 assert(Serial.output.find("type=SUSPECTED_SUDDEN_ROLL")!=std::string::npos);
 assert(hapticPriority==3);startPattern(1);assert(hapticPriority==3);
 auto c=GaitEvents::make(GaitEvents::Contact,500);c.data.contact={1000,160,0,0,GaitEvents::Calibrated|GaitEvents::ValidInterval,20,5};
 processPacket(c);assert(Serial.output.find("type=WITHIN_BASELINE")!=std::string::npos);
 auto impact=GaitEvents::make(GaitEvents::HardLanding,501);
 impact.data.alert={42,8,300,216};
 forwardToPhone(impact);
 assert(phoneNotify->notifications==2);
 assert(GaitEvents::decode(reinterpret_cast<const uint8_t*>(phoneNotify->value.data()),phoneNotify->value.size(),phonePacket));
 assert(phonePacket.kind==GaitEvents::HardLanding && phonePacket.data.alert.sequence==8);
 auto ack=GaitEvents::make(GaitEvents::Ack,600);ack.data.ack={42,'r',1,GaitEvents::Running,0,20,0};
 processPacket(ack);assert(!seenBaseline);
 BLERemoteCharacteristic fake;control=&fake;subscribed=true;handlePcCommand('r');assert(fake.written=="r");
 for(int i=0;i<70;++i)receivePacket(nullptr,reinterpret_cast<uint8_t*>(&raw),20,true);
 assert(rawDrops.load()==6);
 receivePacket(nullptr,reinterpret_cast<uint8_t*>(&raw),19,true);assert(invalidPackets.load()==1);
 auto newStatus=GaitEvents::make(GaitEvents::Status,100);newStatus.data.status={43,GaitEvents::Running,0,0,0};
 processPacket(newStatus);assert(bootSession==43&&!seenAlert&&!seenBaseline);
 std::cout<<"PASS wrist session gate, numeric units, alert labels, haptic priority, BLE controls, overflow accounting\n";

 // Reproduce the failure: BLE connects, the data characteristic exists,
 // but the cached service has no writable control characteristic.
 subscribed=false;
 stopPattern();
 client->service.controlAvailable=false;
 discovered.store(new BLEAdvertisedDevice);
 connectFoot();
 assert(!subscribed && disconnected.load() && refreshPending);
 assert(Serial.output.find("data=READY control=MISSING")!=std::string::npos);
 loop();
 assert(mockGattRefreshes==1 && !refreshPending);

 // A persistently missing control endpoint should permit verified data
 // collection, with command forwarding disabled until a full reconnect.
 discovered.store(new BLEAdvertisedDevice);
 connectFoot();
 assert(subscribed && !sessionVerified && control==nullptr);
 handlePcCommand('w');
 assert(Serial.output.find("WAIT_FOR_VALID_FOOT_DATA")!=std::string::npos);
 processPacket(newStatus);
 assert(sessionVerified);
 handlePcCommand('w');
 assert(Serial.output.find("BLE_CONTROL_UNAVAILABLE")!=std::string::npos);
 client->disconnect();
 loop();

 // A healthy reconnect uses newly discovered handles. The wrist remains
 // unverified until it sees a valid session packet.
 client->service.controlAvailable=true;
 discovered.store(new BLEAdvertisedDevice);
 connectFoot();
 assert(client->discoveries==3 && subscribed && !sessionVerified && control);
 processPacket(newStatus);
 handlePcCommand('w');
 assert(control->written=="w" && !refreshAttempted);

 // Local motor test completes even while the foot is disconnected.
 client->disconnect();
 loop();
 handlePcCommand('v');
 assert(hapticPhases==5);
 for (int i=0;i<5;++i){mockNow+=PulseMs;loop();}
 assert(hapticPhases==0);
 std::cout<<"PASS wrist GATT refresh, read-only recovery, verified reconnect and offline motor test\n";
}
