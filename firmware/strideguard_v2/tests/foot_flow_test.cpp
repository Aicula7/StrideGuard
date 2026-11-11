#include <cassert>
#include <iostream>
#include "../foot_gait_monitor/foot_gait_monitor.ino"
int main(){
 setup();assert(monitoring&&!rollConfirmed&&rollAxis=='x');
 mockAcceleration.acceleration.z=GaitMonitor::Gravity;
 connected.store(true);loop();
 int raws=0,contacts=0,statuses=0,baselines=0;
 auto drain=[&](){GaitEvents::Packet p;while(xQueueReceive(priorityQueue,&p,0)==pdTRUE){
  if(p.kind==GaitEvents::Contact)++contacts;
  if(p.kind==GaitEvents::Status)++statuses;
  if(p.kind==GaitEvents::Baseline)++baselines;
 }while(xQueueReceive(rawQueue,&p,0)==pdTRUE){assert(p.kind==GaitEvents::Raw);++raws;}};
 for(int i=0;i<400;++i){mockNow+=10;loop();drain();}
 assert(settled&&raws>100&&statuses>=3);
 for(int i=0;i<21;++i)for(int k=0;k<100;++k){mockNow+=10;mockAcceleration.acceleration.z=(k==10?1.6f:1.0f)*GaitMonitor::Gravity;loop();drain();}
 assert(detector.calibrated()&&contacts==21&&baselines>=1&&sampleGaps==0);
 connected.store(false);loop();assert(detector.calibrated());
 connected.store(true);loop();assert(!settled&&detector.calibrated());
 handleCommand('p');assert(!monitoring&&detector.calibrated());
 handleCommand('g');assert(monitoring&&detector.calibrated());
 handleCommand('y');assert(rollConfirmed&&rollAxis=='y'&&!detector.calibrated());
 handleCommand('n');assert(!rollConfirmed);
 handleCommand('d');assert(detector.settings().baselineTarget==8);
 handleCommand('w');assert(detector.settings().baselineTarget==20);
 mockSensorOk=false;mockNow+=10;loop();assert(badSamples==1);
 std::cout<<"PASS foot auto startup, wireless trace/contact/baseline, reconnect, remote controls, IMU error\n";
}
