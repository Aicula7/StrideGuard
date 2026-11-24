#include <cassert>
#include <cmath>
#include <iostream>
#include "../foot_gait_monitor/GaitMonitor.h"
#include "../foot_gait_monitor/GaitEventProtocol.h"
int main(){
 using namespace GaitMonitor;
 Detector d;uint32_t t=0;int ready=0,timing=0,hard=0,roll=0,contacts=0,accepted=0;
 auto step=[&](unsigned interval,float peak,float rate,bool enable=true,bool clipped=false){
  Result last;
  for(unsigned k=0;k<interval;k+=10){t+=10;auto r=d.observe(t,k==100?peak:1.0f,(k>=100&&k<=130)?rate:0.0f,clipped&&k==100,enable);
   ready+=r.baselineReady;timing+=r.timingAlert;hard+=r.hardLanding;roll+=r.suddenRoll;contacts+=r.contact;accepted+=r.baselineAccepted;if(r.contact)last=r;
  }return last;
 };
 for(int i=0;i<21;++i)step(1000,1.6f,0);
 assert(d.calibrated()&&ready==1&&accepted==20&&contacts==21);
 assert(d.baselineStrideMs()==1000&&d.strideToleranceMs()==150);
 assert(std::fabs(d.impactThresholdG()-2.16f)<0.001f);
 assert(timing==0&&hard==0&&roll==0);
 for(int i=0;i<8;++i)step(1300,1.6f,0);
 assert(timing==1); // active alert does not retrigger each step
 for(int i=0;i<7;++i)step(1000,1.6f,0);
 for(int i=0;i<8;++i)step(1300,1.6f,0);
 assert(timing==2); // reset after a full normal window
 step(1000,3.0f,0);assert(hard>=1);
 int before=roll;step(1000,1.6f,400,false);assert(roll==before);
 step(1000,1.6f,400,true);assert(roll==before+1);
 auto clipped=step(1000,8.0f,400,true,true);assert(clipped.clipped&&!clipped.suddenRoll&&!clipped.hardLanding);
 auto gap=d.observe(t+40,1.0f,0,false);assert(gap.gap&&d.calibrated());
 d.interrupt();assert(d.calibrated());
 auto first=step(1000,1.6f,0);assert(!first.intervalValid);
 step(2200,1.6f,0);auto slow=step(2200,1.6f,0);assert(!slow.intervalValid);
 d.reset();assert(!d.calibrated());
 Detector handheld(Profile::Handheld);assert(handheld.settings().baselineTarget==8);
 auto p=GaitEvents::make(GaitEvents::Raw,123);p.data.raw={-1000,2000,3000,-15000,14000,1};
 GaitEvents::Packet out;
 assert(sizeof(p)==20&&GaitEvents::decode(reinterpret_cast<uint8_t*>(&p),sizeof(p),out));
 assert(out.data.raw.ax==-1000&&out.data.raw.gx==-15000&&out.timestampMs==123);
 assert(!GaitEvents::decode(nullptr,20,out));
 assert(!GaitEvents::decode(reinterpret_cast<uint8_t*>(&p),19,out));
 p.version=1;assert(!GaitEvents::decode(reinterpret_cast<uint8_t*>(&p),20,out));
 assert(GaitEvents::i16(-99999)==-32768&&GaitEvents::i16(99999)==32767&&GaitEvents::u16(99999)==65535);
 std::cout<<"PASS detector calibration, persistence/rearm, impact, roll gate, clipping, interruption, long intervals, protocol\n";
}
