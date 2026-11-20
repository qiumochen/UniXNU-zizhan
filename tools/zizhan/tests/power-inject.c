#include "ZizhanPowerInjectPlan.h"
#include <stdio.h>

static int failures;

#define CHECK(cond,name) do{ \
 if(cond)printf("PASS %s\n",name); \
 else{printf("FAIL %s\n",name);++failures;} \
}while(0)

int main(void){
 CHECK(zizhan_power_inject_mode(0,0)==ZizhanPowerInjectOff,"inject default off");
 CHECK(zizhan_power_inject_mode(0,1)==ZizhanPowerInjectOff,"inject 0 stays off");
 CHECK(zizhan_power_inject_mode(1,0)==ZizhanPowerInjectSkipObserve,
       "inject needs observe");
 CHECK(zizhan_power_inject_mode(1,2)==ZizhanPowerInjectSkipObserve,
       "observe must be exactly 1");
 CHECK(zizhan_power_inject_mode(1,1)==ZizhanPowerInjectRun,"inject runs with observe");
 CHECK(zizhan_power_inject_mode(2,1)==ZizhanPowerInjectRun,"inject 2 runs two cycles");
 CHECK(zizhan_power_inject_mode(10,1)==ZizhanPowerInjectRun,"inject 10 runs ten cycles");
 CHECK(zizhan_power_inject_mode(11,1)==ZizhanPowerInjectSkipMode,"more than ten cycles skips");
 CHECK(zizhan_power_inject_mode(ZizhanPowerStandbyMode,1)==ZizhanPowerInjectRun,"standby mode 100 runs");
 CHECK(zizhan_power_inject_mode(99,1)==ZizhanPowerInjectSkipMode,"other large values still skip");
 CHECK(zizhan_power_inject_presses(ZizhanPowerStandbyMode)==2u,"standby: off press and on press");
 CHECK(zizhan_power_inject_gap_ms(ZizhanPowerStandbyMode)==480000u,"standby default 480 s");
 CHECK(zizhan_power_inject_gap_ms_s(ZizhanPowerStandbyMode,5)==30000u,"standby clamps to at least 30 s");
 CHECK(zizhan_power_inject_gap_ms_s(ZizhanPowerStandbyMode,5000)==1200000u,"standby clamps to at most 1200 s");
 CHECK(zizhan_power_inject_gap_ms_s(10,600)==5000u,"standby seconds do not change the cycle mode");
 CHECK(zizhan_power_inject_presses(1)==2u&&zizhan_power_inject_gap_ms(1)==8000u,"inject 1 keeps the S1 schedule");
 CHECK(zizhan_power_inject_presses(10)==20u&&zizhan_power_inject_gap_ms(10)==5000u,"ten cycles: 20 presses 5 s apart");
 CHECK(20000u+zizhan_power_inject_presses(10)*(ZizhanPowerPressHoldMs+zizhan_power_inject_gap_ms(10))<150000u,"ten cycles fit well inside the autoreset window");

 CHECK(ZizhanPowerInjectDelayMs==20000u,"first frame delay is 20 s");
 CHECK(ZizhanPowerPressCount==2u,"exactly two presses");
 CHECK(ZizhanPowerPressHoldMs<=200u,"hold is at most 200 ms");
 CHECK(ZizhanPowerPressGapMs>=5000u,"gap is at least 5 s");

 CHECK(zizhan_power_inject_down_ms(0)==0u,"press 1 down at 0 ms");
 CHECK(zizhan_power_inject_up_ms(0)==120u,"press 1 up at 120 ms");
 CHECK(zizhan_power_inject_down_ms(1)==8120u,"press 2 down at 8120 ms");
 CHECK(zizhan_power_inject_up_ms(1)==8240u,"press 2 up at 8240 ms");
 CHECK(zizhan_power_inject_up_ms(0)-zizhan_power_inject_down_ms(0)<=200u,
       "press 1 hold <=200 ms");
 CHECK(zizhan_power_inject_up_ms(1)-zizhan_power_inject_down_ms(1)<=200u,
       "press 2 hold <=200 ms");
 CHECK(zizhan_power_inject_down_ms(1)-zizhan_power_inject_up_ms(0)>=5000u,
       "between presses >=5 s");
 CHECK(zizhan_power_inject_up_ms(1)>zizhan_power_inject_down_ms(1),
       "up follows down");

 printf("%s: %d failure(s)\n",failures?"FAIL":"PASS",failures);
 return failures?1:0;
}
