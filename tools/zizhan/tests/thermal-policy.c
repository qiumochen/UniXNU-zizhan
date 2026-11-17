#include "ZizhanThermalPolicy.h"
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond,name) do{ \
 if(cond)printf("PASS %s\n",name); \
 else{printf("FAIL %s\n",name);++failures;} \
}while(0)

static ZizhanThermalSample base(uint64_t t){
 ZizhanThermalSample s;
 memset(&s,0,sizeof s);
 s.time_us=t;
 s.bank_valid[0]=1;s.bank_valid[1]=1;
 s.valid_mask[0]=0xffffu;s.valid_mask[1]=0xffffu;
 for(unsigned b=0;b<ZizhanThermalBanks;++b)
  for(unsigned i=0;i<ZizhanThermalSensors;++i)s.temp_mc[b][i]=40000;
 return s;
}

static void cpu(ZizhanThermalSample *s,unsigned n,int32_t mc){
 unsigned b=0,i=0;
 if(zizhan_thermal_cpu_sensor(n,&b,&i))s->temp_mc[b][i]=mc;
}

static void gpu(ZizhanThermalSample *s,unsigned n,int32_t mc){
 unsigned b=0,i=0;
 if(zizhan_thermal_gpu_sensor(n,&b,&i))s->temp_mc[b][i]=mc;
}

static void drop_cpu(ZizhanThermalSample *s,unsigned n){
 unsigned b=0,i=0;
 if(zizhan_thermal_cpu_sensor(n,&b,&i))s->valid_mask[b]&=(uint16_t)~(1u<<i);
}

static void drop_gpu(ZizhanThermalSample *s,unsigned n){
 unsigned b=0,i=0;
 if(zizhan_thermal_gpu_sensor(n,&b,&i))s->valid_mask[b]&=(uint16_t)~(1u<<i);
}

static int step(ZizhanThermalState *st,uint64_t t,ZizhanThermalSample *s){
 ZizhanThermalConfig c=zizhan_thermal_default_config();
 s->time_us=t;
 return zizhan_thermal_step(&c,st,s);
}

static void test_mapping(void){
 unsigned b=0,i=0;
 CHECK(zizhan_thermal_cpu_sensor(0,&b,&i)&&b==0&&i==5,"mapping cpu0");
 CHECK(zizhan_thermal_cpu_sensor(8,&b,&i)&&b==0&&i==13,"mapping cpu8");
 CHECK(zizhan_thermal_cpu_sensor(9,&b,&i)&&b==1&&i==1,"mapping cpu9");
 CHECK(zizhan_thermal_cpu_sensor(12,&b,&i)&&b==1&&i==4,"mapping cpu12");
 CHECK(!zizhan_thermal_cpu_sensor(13,&b,&i),"mapping cpu13 invalid");
 CHECK(zizhan_thermal_gpu_sensor(0,&b,&i)&&b==0&&i==14,"mapping gpu0");
 CHECK(zizhan_thermal_gpu_sensor(1,&b,&i)&&b==0&&i==15,"mapping gpu1");
 CHECK(!zizhan_thermal_gpu_sensor(2,&b,&i),"mapping gpu2 invalid");
}

static void test_normal(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);
 CHECK(step(&st,1000000,&s)==ZizhanThermalFresh,"normal fresh");
 CHECK(st.cpu_level==0&&st.gpu_level==0,"normal no cap");
}

static void test_cpu_hot(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,4,110000);
 CHECK(step(&st,1000000,&s)==ZizhanThermalFresh,"cpu hot fresh");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels&&st.gpu_level==0,"cpu hot lowest");
 CHECK(st.cpu_fresh==0,"cpu hot clears streak");
}

static void test_cpu_hysteresis(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,4,110000);step(&st,1000000,&s);
 for(unsigned n=1;n<=3;++n){
  s=base(1000000ull+1000000ull*n);cpu(&s,4,106000);
  step(&st,s.time_us,&s);
 }
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"cpu 106 holds");
 for(unsigned n=1;n<=2;++n){
  s=base(4000000ull+1000000ull*n);cpu(&s,4,104000);
  step(&st,s.time_us,&s);
 }
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"cpu 104 two samples no release");
 s=base(7000000);cpu(&s,4,104000);step(&st,7000000,&s);
 CHECK(st.cpu_level==ZizhanThermalCpuLevels-1,"cpu 104 third sample releases one");
 CHECK(st.cpu_fresh==0,"cpu release restarts streak");
}

static void test_gpu_hysteresis(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);gpu(&s,0,95000);step(&st,1000000,&s);
 CHECK(st.gpu_level==ZizhanThermalGpuLevels,"gpu hot lowest");
 for(unsigned n=1;n<=3;++n){
  s=base(1000000ull+1000000ull*n);gpu(&s,0,90000);
  step(&st,s.time_us,&s);
 }
 CHECK(st.gpu_level==ZizhanThermalGpuLevels,"gpu 90 holds (needs <90)");
 for(unsigned n=1;n<=3;++n){
  s=base(4000000ull+1000000ull*n);gpu(&s,0,89999);
  step(&st,s.time_us,&s);
 }
 CHECK(st.gpu_level==ZizhanThermalGpuLevels-1,"gpu 89 releases one");
}

static void test_stale_hold(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,4,110000);step(&st,1000000,&s);
 s=base(3000001); /* > 2 periods after 1.000000 */
 CHECK(step(&st,3000001,&s)==ZizhanThermalStale,"stale returns stale");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"stale holds cpu cap");
 CHECK(st.cpu_fresh==0&&st.gpu_fresh==0,"stale clears streaks");
}

static void test_invalid_sensor_blocks_release(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,4,110000);step(&st,1000000,&s);
 for(unsigned n=1;n<=3;++n){
  s=base(1000000ull+1000000ull*n);drop_cpu(&s,5);
  CHECK(step(&st,s.time_us,&s)==ZizhanThermalFresh,"invalid cpu sensor fresh");
 }
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"invalid cpu sensor holds cap");
 for(unsigned n=1;n<=3;++n){
  s=base(4000000ull+1000000ull*n);
  step(&st,s.time_us,&s);
 }
 CHECK(st.cpu_level==ZizhanThermalCpuLevels-1,"valid cpu sensors can release");
}

static void test_bank_invalid_blocks_release(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,4,110000);step(&st,1000000,&s);
 for(unsigned n=1;n<=3;++n){
  s=base(1000000ull+1000000ull*n);s.bank_valid[1]=0;
  step(&st,s.time_us,&s);
 }
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"invalid bank holds cpu cap");
 for(unsigned n=1;n<=3;++n){
  s=base(4000000ull+1000000ull*n);
  step(&st,s.time_us,&s);
 }
 CHECK(st.cpu_level==ZizhanThermalCpuLevels-1,"valid banks release");
}

static void test_out_of_order_and_wrap(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(5000000);cpu(&s,4,110000);step(&st,5000000,&s);
 s=base(4000000);
 CHECK(step(&st,4000000,&s)==ZizhanThermalStale,"out of order stale");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"out of order holds cap");
 s=base(6000000);
 CHECK(step(&st,6000000,&s)==ZizhanThermalFresh,"in order after out-of-order fresh");
 ZizhanThermalState w;zizhan_thermal_init(&w);
 w.have_last=1;w.last_time_us=UINT64_MAX-100;
 s=base(100);
 CHECK(step(&w,100,&s)==ZizhanThermalStale,"wrap/backwards stale");
 CHECK(w.cpu_level==0&&w.gpu_level==0,"wrap leaves caps");
}

static void test_invalid_hot_does_not_clamp(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,4,120000);drop_cpu(&s,4);
 CHECK(step(&st,1000000,&s)==ZizhanThermalFresh,"invalid hot fresh");
 CHECK(st.cpu_level==0,"invalid hot does not clamp");
 s=base(2000000);cpu(&s,4,120000);
 step(&st,2000000,&s);
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"next valid hot clamps");
}

static void test_both_release(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,4,110000);gpu(&s,0,95000);
 step(&st,1000000,&s);
 CHECK(st.cpu_level==ZizhanThermalCpuLevels&&st.gpu_level==ZizhanThermalGpuLevels,"both clamp");
 for(unsigned n=1;n<=3;++n){
  s=base(1000000ull+1000000ull*n);
  step(&st,s.time_us,&s);
 }
 CHECK(st.cpu_level==ZizhanThermalCpuLevels-1&&st.gpu_level==ZizhanThermalGpuLevels-1,"both release one");
}

static void test_invalid_config(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalState before=st;
 ZizhanThermalConfig c=zizhan_thermal_default_config();
 c.cpu_clamp_mc=100000;c.cpu_release_mc=105000;
 ZizhanThermalSample s=base(1000000);cpu(&s,4,110000);
 CHECK(zizhan_thermal_step(&c,&st,&s)==ZizhanThermalInvalid,"bad thresholds invalid");
 CHECK(memcmp(&st,&before,sizeof st)==0,"bad config leaves state");
 c=zizhan_thermal_default_config();c.period_us=0;
 CHECK(zizhan_thermal_step(&c,&st,&s)==ZizhanThermalInvalid,"zero period invalid");
}

static void test_first_sample_hot(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,0,110000);
 CHECK(step(&st,1000000,&s)==ZizhanThermalFresh,"first hot fresh");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"first hot clamps");
}

static void test_gpu_missing(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);gpu(&s,0,95000);step(&st,1000000,&s);
 for(unsigned n=1;n<=3;++n){
  s=base(1000000ull+1000000ull*n);drop_gpu(&s,1);
  step(&st,s.time_us,&s);
 }
 CHECK(st.gpu_level==ZizhanThermalGpuLevels,"missing gpu sensor holds cap");
}

int main(void){
 test_mapping();
 test_normal();
 test_cpu_hot();
 test_cpu_hysteresis();
 test_gpu_hysteresis();
 test_stale_hold();
 test_invalid_sensor_blocks_release();
 test_bank_invalid_blocks_release();
 test_out_of_order_and_wrap();
 test_invalid_hot_does_not_clamp();
 test_both_release();
 test_invalid_config();
 test_first_sample_hot();
 test_gpu_missing();
 printf("%s: %d failure(s)\n",failures?"FAIL":"PASS",failures);
 return failures?1:0;
}
