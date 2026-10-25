#include "DlssStartupGate.h"
#include "DlssModes.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks{};
static void check(bool value,const char* label){++checks;if(!value){fprintf(stderr,"FAIL: %s\n",label);exit(1);}}
int main(){
 check(DlssModes::Configured(true,3)==DlssModes::Balanced,"release mode is Balanced");
 DlssStartupGate gate;
 check(!gate.Update(0,false),"no startup activation before gameplay");
 check(!gate.Update(100000,false),"menu time does not satisfy startup gate");
 check(!gate.Update(100001,true),"first tracked gameplay frame remains native");
 check(!gate.Update(110000,true),"full ten seconds required");
 check(!gate.Update(110001,false,true),"actual menu interruption restarts settling");
 check(!gate.Update(120000,true),"new gameplay interval starts natively");
 check(!gate.Update(129999,true),"interrupted interval not counted");
 check(gate.Update(130000,true),"Balanced may activate automatically after settling");
 check(gate.Update(130001,false),"accepted DLSS menu behavior retained after startup");
 check(gate.Update(130002,true),"later F3 mode changes need no startup delay");
 DlssStartupGate emptyIntervals;
 check(!emptyIntervals.Update(0,true),"world interval starts timer at tick zero");
 for(uint64_t tick=100;tick<10000;tick+=100){check(!emptyIntervals.Update(tick,false),"empty renderer calls remain native");check(!emptyIntervals.Update(tick,true),"interleaved world calls continue settling");}
 check(emptyIntervals.Update(10000,true),"empty intervals cannot postpone automatic startup indefinitely");
 DlssStartupGate lost;
 check(!lost.Update(100,true) && !lost.Update(2201,false),"extended tracking/loading gap resets settling");
 check(!lost.Update(3000,true) && !lost.Update(12999,true),"new stable interval required after long gap");
 check(lost.Update(13000,true),"startup resumes after stable tracked gameplay");
 printf("PASS: %u release-default and automatic DLSS startup checks.\n",checks);
}
