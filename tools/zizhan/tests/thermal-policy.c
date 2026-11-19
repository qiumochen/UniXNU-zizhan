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

static ZizhanThermalConfig tuned(const ZizhanThermalTuning *t){
 ZizhanThermalConfig c=zizhan_thermal_default_config();
 c.cpu_clamp_mc=t->cpu_clamp_mc;c.cpu_release_mc=t->cpu_release_mc;
 c.gpu_clamp_mc=t->gpu_clamp_mc;c.gpu_release_mc=t->gpu_release_mc;
 return c;
}

static int step_cfg(const ZizhanThermalConfig *c,ZizhanThermalState *st,uint64_t t,
                    ZizhanThermalSample *s){
 s->time_us=t;
 return zizhan_thermal_step(c,st,s);
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

static void test_stepped_clamp(void){
 ZizhanThermalConfig c=zizhan_thermal_default_config();c.clamp_step=4;
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,4,110000);
 CHECK(step_cfg(&c,&st,1000000,&s)==ZizhanThermalFresh&&st.cpu_level==4,"stepped clamp first hot +4");
 s=base(2000000);cpu(&s,4,111000);
 CHECK(step_cfg(&c,&st,2000000,&s)==ZizhanThermalFresh&&st.cpu_level==8,"stepped clamp second hot +4");
 s=base(3000000);cpu(&s,4,107000);
 CHECK(step_cfg(&c,&st,3000000,&s)==ZizhanThermalFresh&&st.cpu_level==8,"stepped clamp holds in hysteresis band");
 s=base(4000000);cpu(&s,4,120000);
 CHECK(step_cfg(&c,&st,4000000,&s)==ZizhanThermalFresh&&st.cpu_level==ZizhanThermalCpuLevels,"critical jumps to lowest");
 ZizhanThermalState g;zizhan_thermal_init(&g);
 for(unsigned i=0;i<10;++i){s=base(1000000ull*(i+1));gpu(&s,1,96000);step_cfg(&c,&g,1000000ull*(i+1),&s);}
 CHECK(g.gpu_level==ZizhanThermalGpuLevels,"stepped gpu saturates at lowest");
 c.clamp_step=0;ZizhanThermalState o;zizhan_thermal_init(&o);s=base(1000000);cpu(&s,4,110000);step_cfg(&c,&o,1000000,&s);
 CHECK(o.cpu_level==ZizhanThermalCpuLevels,"step 0 keeps v1 jump");
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

static void test_slow_sampling_still_clamps(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);step(&st,1000000,&s);
 /* Every sample is 3 periods after the last: stale, but a hot reading must
  * still clamp and the cap must never be released by stale cold samples. */
 s=base(4000000);cpu(&s,4,110000);
 CHECK(step(&st,4000000,&s)==ZizhanThermalStale,"3-period hot returns stale");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"3-period hot still clamps");
 for(unsigned n=1;n<=4;++n){
  s=base(4000000ull+3000000ull*n);
  CHECK(step(&st,s.time_us,&s)==ZizhanThermalStale,"3-period cold stale");
 }
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"3-period sampling never releases");
 CHECK(st.cpu_fresh==0,"3-period stale clears streak");
}

static void test_backwards_rebase_and_recover(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(10000000);step(&st,10000000,&s);
 /* Clock goes backwards: the hot reading still clamps, the baseline moves
  * to the new time, and three fresh cold samples release one level. */
 s=base(9000000);cpu(&s,4,110000);
 CHECK(step(&st,9000000,&s)==ZizhanThermalStale,"backwards hot returns stale");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"backwards hot still clamps");
 s=base(10000000);step(&st,10000000,&s);
 s=base(11000000);step(&st,11000000,&s);
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"backwards rebase two fresh no release");
 s=base(12000000);
 CHECK(step(&st,12000000,&s)==ZizhanThermalFresh,"backwards rebase third fresh");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels-1,"backwards rebase releases after three");
}

static void test_stale_cold_no_streak(void){
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,4,110000);step(&st,1000000,&s);
 s=base(4000000);
 CHECK(step(&st,4000000,&s)==ZizhanThermalStale,"stale cold returns stale");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"stale cold holds cap");
 CHECK(st.cpu_fresh==0,"stale cold does not advance streak");
 s=base(5000000);step(&st,5000000,&s);
 s=base(6000000);step(&st,6000000,&s);
 CHECK(st.cpu_level==ZizhanThermalCpuLevels,"two fresh after stale no release");
 s=base(7000000);step(&st,7000000,&s);
 CHECK(st.cpu_level==ZizhanThermalCpuLevels-1,"third fresh after stale releases");
}

/* g396 one-shot real idle readings, docs/evidence/2026-11-07-tsens-readonly.txt:
 * bank0 sensor 0..15 and bank1 sensor 0..15, decoded milliC, all VALID. */
static void test_real_idle_g396(void){
 static const int32_t bank0[16]={39800,41000,39800,41000,41800,42200,46900,39800,
                                 39800,39400,39000,41400,41800,40600,38200,39400};
 static const int32_t bank1[16]={39100,38700,39500,38300,39100,37500,35500,36300,
                                 37500,38700,38300,35900,36300,37500,36700,37100};
 ZizhanThermalSample s;
 memset(&s,0,sizeof s);
 s.time_us=1000000;s.bank_valid[0]=1;s.bank_valid[1]=1;
 s.valid_mask[0]=0xffffu;s.valid_mask[1]=0xffffu;
 for(unsigned i=0;i<16;++i){s.temp_mc[0][i]=bank0[i];s.temp_mc[1][i]=bank1[i];}
 ZizhanThermalState st;zizhan_thermal_init(&st);
 CHECK(step(&st,1000000,&s)==ZizhanThermalFresh,"g396 real idle fresh");
 CHECK(st.cpu_level==0&&st.gpu_level==0,"g396 real idle no cap");

 /* Only cpu-1-1 (bank0 sensor 6) is changed to 110 C. */
 ZizhanThermalState hot;zizhan_thermal_init(&hot);
 ZizhanThermalSample h=s;
 h.temp_mc[0][6]=110000;
 CHECK(step(&hot,1000000,&h)==ZizhanThermalFresh,"g396 one hot fresh");
 CHECK(hot.cpu_level==ZizhanThermalCpuLevels,"g396 one cpu-1 hot clamps");
 CHECK(hot.gpu_level==0,"g396 one cpu-1 hot leaves gpu");
}

static void test_tuning_default(void){
 ZizhanThermalTuning t;
 memset(&t,0,sizeof t);
 CHECK(zizhan_thermal_tune(1,0,0,0,&t)==ZizhanThermalTuneOk,"tune default ok");
 CHECK(t.mode==1&&t.test_cpu_c==0&&t.test_gpu_c==0&&t.load_seconds==0,
       "tune default fields");
 CHECK(t.cpu_clamp_mc==110000&&t.cpu_release_mc==105000,"tune default cpu");
 CHECK(t.gpu_clamp_mc==95000&&t.gpu_release_mc==90000,"tune default gpu");
}

static void test_cpu_rank(void){
 CHECK(zizhan_thermal_cpu_rank(0,20)==0,"rank level 0");
 CHECK(zizhan_thermal_cpu_rank(20,20)==19,"rank level 20 lowest of 20");
 CHECK(zizhan_thermal_cpu_rank(1,20)==1,"rank level 1 second highest");
 CHECK(zizhan_thermal_cpu_rank(10,20)==10,"rank level 10 middle");
 CHECK(zizhan_thermal_cpu_rank(99,20)==19,"rank clamps high level");
 CHECK(zizhan_thermal_cpu_rank(20,2)==1,"rank two rows lowest");
 CHECK(zizhan_thermal_cpu_rank(1,2)==0,"rank two rows highest");
 CHECK(zizhan_thermal_cpu_rank(20,1)==0,"rank one row has no cap");
}

static void test_cpu_cap_direction(void){
 static const unsigned rows[5]={300000,600000,900000,1200000,1500000};
 static const unsigned sparse[9]={300000,600000,0,900000,0,1200000,0,0,1500000};
 CHECK(zizhan_thermal_cpu_cap_khz(0,rows,5)==0,"cap level 0 takes no cap");
 CHECK(zizhan_thermal_cpu_cap_khz(20,rows,5)==300000,"cap level 20 takes lowest");
 CHECK(zizhan_thermal_cpu_cap_khz(1,rows,5)==1500000,"cap level 1 takes highest");
 unsigned previous=~0u;
 for(unsigned level=1;level<=20;++level){
  const unsigned khz=zizhan_thermal_cpu_cap_khz(level,rows,5);
  CHECK(khz&&khz<=previous,"cap monotonic with level");
  previous=khz;
 }
 CHECK(zizhan_thermal_cpu_cap_khz(20,sparse,9)==300000,"cap sparse level 20 lowest");
 CHECK(zizhan_thermal_cpu_cap_khz(1,sparse,9)==1500000,"cap sparse level 1 highest");
 CHECK(zizhan_thermal_cpu_cap_khz(20,rows,1)==0,"cap one row takes no cap");
 CHECK(zizhan_thermal_cpu_cap_khz(20,NULL,5)==0,"cap null takes no cap");
}

static void test_tuning_modes(void){
 ZizhanThermalTuning t;
 for(unsigned m=0;m<=2;++m)
  CHECK(zizhan_thermal_tune(m,0,0,0,&t)==ZizhanThermalTuneOk,"tune mode ok");
 CHECK(zizhan_thermal_tune(3,0,0,0,&t)==ZizhanThermalTuneBadMode,"tune mode 3 rejected");
 CHECK(zizhan_thermal_tune(UINT32_MAX,0,0,0,&t)==ZizhanThermalTuneBadMode,
       "tune mode max rejected");
 CHECK(zizhan_thermal_tune(1,0,0,0,NULL)==ZizhanThermalTuneBadOutput,
       "tune null output rejected");
}

static void test_tuning_cpu(void){
 ZizhanThermalTuning t;
 CHECK(zizhan_thermal_tune(2,39,0,0,&t)==ZizhanThermalTuneBadCpu,"tune cpu 39 rejected");
 CHECK(zizhan_thermal_tune(2,40,0,0,&t)==ZizhanThermalTuneOk,"tune cpu 40 accepted");
 CHECK(t.cpu_clamp_mc==40000&&t.cpu_release_mc==35000,"tune cpu 40 release 35");
 CHECK(zizhan_thermal_tune(2,100,0,0,&t)==ZizhanThermalTuneOk,"tune cpu 100 accepted");
 CHECK(t.cpu_clamp_mc==100000&&t.cpu_release_mc==95000,"tune cpu 100 release 95");
 CHECK(zizhan_thermal_tune(2,101,0,0,&t)==ZizhanThermalTuneBadCpu,"tune cpu 101 rejected");
 CHECK(zizhan_thermal_tune(2,110,0,0,&t)==ZizhanThermalTuneBadCpu,"tune cpu 110 rejected");
}

static void test_tuning_gpu(void){
 ZizhanThermalTuning t;
 CHECK(zizhan_thermal_tune(2,0,39,0,&t)==ZizhanThermalTuneBadGpu,"tune gpu 39 rejected");
 CHECK(zizhan_thermal_tune(2,0,40,0,&t)==ZizhanThermalTuneOk,"tune gpu 40 accepted");
 CHECK(t.gpu_clamp_mc==40000&&t.gpu_release_mc==35000,"tune gpu 40 release 35");
 CHECK(zizhan_thermal_tune(2,0,94,0,&t)==ZizhanThermalTuneOk,"tune gpu 94 accepted");
 CHECK(t.gpu_clamp_mc==94000&&t.gpu_release_mc==89000,"tune gpu 94 release 89");
 CHECK(zizhan_thermal_tune(2,0,95,0,&t)==ZizhanThermalTuneBadGpu,
       "tune gpu 95 not below default");
 CHECK(zizhan_thermal_tune(2,0,100,0,&t)==ZizhanThermalTuneBadGpu,
       "tune gpu 100 rejected");
}

static void test_tuning_load(void){
 ZizhanThermalTuning t;
 CHECK(zizhan_thermal_tune(1,0,0,120,&t)==ZizhanThermalTuneOk,"tune load 120 accepted");
 CHECK(t.load_seconds==120,"tune load field");
 CHECK(zizhan_thermal_tune(1,0,0,121,&t)==ZizhanThermalTuneBadLoad,"tune load 121 rejected");
 CHECK(zizhan_thermal_tune(0,0,0,1,&t)==ZizhanThermalTuneBadLoad,
       "tune load without mode rejected");
}

static void test_tuning_leaves_output(void){
 ZizhanThermalTuning t;
 memset(&t,0xa5,sizeof t);
 const ZizhanThermalTuning before=t;
 CHECK(zizhan_thermal_tune(2,39,0,0,&t)==ZizhanThermalTuneBadCpu,"tune reject cpu");
 CHECK(memcmp(&t,&before,sizeof t)==0,"tune reject leaves output");
 CHECK(zizhan_thermal_tune(2,0,95,0,&t)==ZizhanThermalTuneBadGpu,"tune reject gpu");
 CHECK(memcmp(&t,&before,sizeof t)==0,"tune gpu reject leaves output");
 CHECK(zizhan_thermal_tune(2,0,0,121,&t)==ZizhanThermalTuneBadLoad,"tune reject load");
 CHECK(memcmp(&t,&before,sizeof t)==0,"tune load reject leaves output");
}

static void test_tuned_stale_invalid_hold(void){
 ZizhanThermalTuning t;
 CHECK(zizhan_thermal_tune(2,50,60,0,&t)==ZizhanThermalTuneOk,"tuned 50/60 ok");
 const ZizhanThermalConfig c=tuned(&t);
 ZizhanThermalState st;zizhan_thermal_init(&st);
 ZizhanThermalSample s=base(1000000);cpu(&s,4,50000);gpu(&s,0,60000);
 CHECK(step_cfg(&c,&st,1000000,&s)==ZizhanThermalFresh,"tuned hot fresh");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels&&st.gpu_level==ZizhanThermalGpuLevels,
       "tuned hot clamps");

 /* One stale cold sample and one invalid cold sample: neither may release. */
 s=base(4000000);cpu(&s,4,40000);gpu(&s,0,50000);
 CHECK(step_cfg(&c,&st,4000000,&s)==ZizhanThermalStale,"tuned stale cold stale");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels&&st.gpu_level==ZizhanThermalGpuLevels,
       "tuned stale cold holds");
 s=base(5000000);cpu(&s,4,40000);gpu(&s,0,50000);drop_cpu(&s,0);drop_gpu(&s,1);
 CHECK(step_cfg(&c,&st,5000000,&s)==ZizhanThermalFresh,"tuned invalid cold fresh");
 CHECK(st.cpu_level==ZizhanThermalCpuLevels&&st.gpu_level==ZizhanThermalGpuLevels,
       "tuned invalid cold holds");

 /* The tuned release thresholds are 45/55 C, not the defaults. */
 for(unsigned n=1;n<=3;++n){
  s=base(5000000ull+1000000ull*n);cpu(&s,4,44000);gpu(&s,0,54000);
  step_cfg(&c,&st,s.time_us,&s);
 }
 CHECK(st.cpu_level==ZizhanThermalCpuLevels-1&&st.gpu_level==ZizhanThermalGpuLevels-1,
       "tuned three fresh below test release");
}

static void test_load_deadline(void){
 ZizhanThermalLoad l;
 zizhan_thermal_load_begin(&l,2000000,60);
 CHECK(l.deadline_us==62000000ull,"load deadline 60 s");
 CHECK(zizhan_thermal_load_step(&l,61000000,40000,ZIZHAN_THERMAL_LOAD_HOT_MC)==
       ZizhanThermalLoadRun,"load before deadline runs");
 CHECK(zizhan_thermal_load_step(&l,62000000,40000,ZIZHAN_THERMAL_LOAD_HOT_MC)==
       ZizhanThermalLoadDeadline,"load deadline stops");
 CHECK(l.stopped&&l.reason==ZizhanThermalLoadDeadline,"load deadline recorded");
 CHECK(zizhan_thermal_load_step(&l,UINT64_MAX,40000,ZIZHAN_THERMAL_LOAD_HOT_MC)==
       ZizhanThermalLoadDeadline,"load stays stopped");
}

static void test_load_zero_and_no_reading(void){
 ZizhanThermalLoad l;
 zizhan_thermal_load_begin(&l,5000000,0);
 CHECK(zizhan_thermal_load_step(&l,5000000,40000,ZIZHAN_THERMAL_LOAD_HOT_MC)==
       ZizhanThermalLoadDeadline,"load zero seconds stops at once");
 zizhan_thermal_load_begin(&l,5000000,120);
 CHECK(zizhan_thermal_load_step(&l,5000000,INT32_MIN,ZIZHAN_THERMAL_LOAD_HOT_MC)==
       ZizhanThermalLoadRun,"load missing reading does not stop");
 CHECK(zizhan_thermal_load_step(&l,125000000,INT32_MIN,ZIZHAN_THERMAL_LOAD_HOT_MC)==
       ZizhanThermalLoadDeadline,"load missing reading stops at deadline");
}

static void test_load_hot_boundary(void){
 ZizhanThermalLoad l;
 zizhan_thermal_load_begin(&l,0,60);
 CHECK(zizhan_thermal_load_step(&l,1000000,74999,75000)==ZizhanThermalLoadRun,
       "load 74.999 C runs");
 CHECK(zizhan_thermal_load_step(&l,2000000,75000,75000)==ZizhanThermalLoadHot,
       "load 75 C stops");
 CHECK(l.stopped&&l.reason==ZizhanThermalLoadHot,"load hot recorded");
 CHECK(zizhan_thermal_load_step(&l,3000000,40000,75000)==ZizhanThermalLoadHot,
       "load hot stop latches");
}

static void test_load_gate(void){
 /* waits for the desktop, a complete sample and a peak below 60 C; gives up after 120 s */
 CHECK(zizhan_thermal_load_gate(0,1,40000,0)==ZizhanThermalLoadWait,"load gate waits for the desktop");
 CHECK(zizhan_thermal_load_gate(1,0,40000,0)==ZizhanThermalLoadWait,"load gate waits for a complete sample");
 CHECK(zizhan_thermal_load_gate(1,1,INT32_MIN,0)==ZizhanThermalLoadWait,"load gate waits for a reading");
 CHECK(zizhan_thermal_load_gate(1,1,60000,0)==ZizhanThermalLoadWait,"load gate: 60 C is not cool");
 CHECK(zizhan_thermal_load_gate(1,1,90700,119999999ull)==ZizhanThermalLoadWait,"load gate: hot keeps waiting");
 CHECK(zizhan_thermal_load_gate(1,1,59999,5000000ull)==ZizhanThermalLoadGo,"load gate: cool sample starts");
 CHECK(zizhan_thermal_load_gate(1,1,90700,120000000ull)==ZizhanThermalLoadGiveUp,"load gate gives up at 120 s");
 CHECK(zizhan_thermal_load_gate(1,1,40000,120000000ull)==ZizhanThermalLoadGiveUp,"load gate deadline wins");
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
 test_stepped_clamp();
 test_invalid_config();
 test_first_sample_hot();
 test_gpu_missing();
 test_slow_sampling_still_clamps();
 test_backwards_rebase_and_recover();
 test_stale_cold_no_streak();
 test_real_idle_g396();
 test_cpu_rank();
 test_cpu_cap_direction();
 test_tuning_default();
 test_tuning_modes();
 test_tuning_cpu();
 test_tuning_gpu();
 test_tuning_load();
 test_tuning_leaves_output();
 test_tuned_stale_invalid_hold();
 test_load_deadline();
 test_load_zero_and_no_reading();
 test_load_hot_boundary();
 test_load_gate();
 printf("%s: %d failure(s)\n",failures?"FAIL":"PASS",failures);
 return failures?1:0;
}
