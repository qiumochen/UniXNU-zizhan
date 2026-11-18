/* Actual init/RAM/coeff/run core under GENI model;never emits hardware sound. */
#define ZIZHAN_COEFF_NO_MAIN
#include "cs35l41-coefficients.c"
#undef ZIZHAN_COEFF_NO_MAIN
static unsigned prepare_run(Fake *f,ZizhanAmpCoefficients *coeff,ZizhanAmpHash hash,ZizhanAmpHash fw_hash){
 ZizhanAmpRam ram;prepare_coeff(f,&ram,fw_hash);
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 const ZizhanAmpClock clock={f,now,pause_us};
 assert(!zizhan_cs35l41_coefficients(1,flags,&f->bus,&clock,&ram,top_coeff,sizeof top_coeff,bottom_coeff,sizeof bottom_coeff,hash,coeff)&&coeff->status==ZizhanAmpPass);
 f->run_mode=1;return f->commands;
}
typedef struct {Fake *f;ZizhanAmpRun *out;unsigned calls,test;} TransferContext;
static int transfer_zero(void *context,unsigned chip){TransferContext *c=context;assert(chip==c->calls++&&chip<2);
 assert(c->f->state[chip][2]==1&&(c->f->state[chip][0]&1)&&!(c->f->state[chip][1]&0x31)&&c->out->samples[chip]==49&&c->out->heartbeat_changes[chip]>=10);
 if(c->test==1&&chip==0){return 0;}if(c->test==2&&chip==1)return 0;
 if(c->test==7)c->f->iv_bad=1;
 if(c->test==4)c->f->state[chip][2]^=0x100;
 if(c->test==5)c->f->run_freeze=1;
 if(c->test==6)c->f->time+=8000000;else c->f->time+=10000;
 return 1;
}
/* Profile 19 window: both amps enabled (or both off when dry) for the whole stream; poll between periods. */
typedef struct {Fake *f;unsigned calls,polls,test,dry;} PlayContext;
static int transfer_play(void *p,unsigned ms,int (*poll)(void *),void *poll_context){PlayContext *c=p;++c->calls;assert(ms==500);
 for(unsigned chip=0;chip<2;++chip){assert(c->f->state[chip][2]==1&&c->f->state[chip][0]==1);assert(c->f->state[chip][1]==(c->dry?0x3300u:0x3321u));}
 for(unsigned period=0;period<50;++period){c->f->time+=10000;
  if((c->test==2&&period==20)||(c->test==4&&period==10)){c->f->play_fault=1;}if(c->test==3&&period==30){c->f->run_halo_state=1;}
  ++c->polls;if(poll(poll_context)){return c->test==4?1:0;}}
 return c->test==5?0:1;
}
typedef struct {Fake *f;unsigned calls,test;} PulseContext;
static int transfer_pulse(void *p,unsigned chip){PulseContext *c=p;const unsigned order[]={2,0,3,1};assert(c->calls<4&&chip==order[c->calls]);
 const unsigned real=chip%2;assert(c->f->state[real][2]==1&&c->f->state[real][0]==1);
 assert(c->f->state[real][1]==(chip>=2?0x3300u:0x3321u));++c->calls;c->f->time+=10000;
 if(c->test==1&&chip==2){return 0;}if(c->test==2&&chip==0)return 0;if(c->test==3&&chip==0)c->f->time+=250000;
 return 1;
}
int main(int argc,char **argv){
 static Fake f;ZizhanAmpCoefficients coeff;ZizhanAmpRun out;
 ZizhanAmpHash fw_hash=synthetic_hash,coeff_hash=coeff_mock_hash;
 for(unsigned i=0;i<sizeof firmware;++i)firmware[i]=(uint8_t)(i*37u+19u);
 for(unsigned i=0;i<sizeof top_coeff;++i)top_coeff[i]=(uint8_t)(i*17+3);
 for(unsigned i=0;i<sizeof bottom_coeff;++i)bottom_coeff[i]=(uint8_t)(i*29+7);
 if(argc==4){read_file(argv[1],firmware,sizeof firmware);read_file(argv[2],top_coeff,sizeof top_coeff);read_file(argv[3],bottom_coeff,sizeof bottom_coeff);fw_hash=coeff_hash=actual_hash;}
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 const ZizhanAmpClock clock={&f,now,pause_us};unsigned commands=0;
 for(unsigned test=0;test<11;++test){
  unsigned before=prepare_run(&f,&coeff,coeff_hash,fw_hash),enabled=1;
  if(test==1){enabled=0;}if(test==2)coeff.readbacks[1]=949;if(test==3)coeff.identity[1][12]++;
  if(test==4){f.run_freeze=1;}if(test==5)f.run_bad_mpu=1;if(test==6)f.run_fault=1;
  if(test==7){f.state[0][1]|=1;}if(test==8)f.clock_step=2000000;
  if(test==9)f.stall_command=before+85,f.frozen=1;
  if(test==10)coeff.transport.controller_uncertain=1;
  int rc=zizhan_cs35l41_run(enabled,flags,&f.bus,&clock,&coeff,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==82&&out.acknowledged_writes==82);
   assert(f.run_rate_writes==32&&f.run_mpu_writes==46&&f.run_core_writes==4);
   for(unsigned c=0;c<2;++c){assert(out.rate_readbacks[c]==16&&out.mpu_readbacks[c]==20&&out.started[c]==1&&out.stopped[c]==1);
    assert(out.samples[c]==3&&out.heartbeat_changes[c]==2&&out.core_running[c]==0x101&&out.core_after[c]==0x100&&f.state[c][0]==0&&f.state[c][1]==0x3300);}
   commands=f.commands-before;
  }else if(test<=3||test==10)assert(!rc&&out.status==ZizhanAmpSkip&&f.commands==before);
  else if(test==4)assert(rc==ZizhanAmpTimeout&&out.started[0]&&out.stopped[0]&&f.state[0][2]==0x100&&!out.completed);
  else if(test==5)assert(rc==ZizhanAmpReadback&&!out.started[0]);
  else if(test==6)assert(rc==ZizhanAmpProtocol&&out.stopped[0]);
  else if(test==7)assert(rc==ZizhanAmpStateChanged&&!out.attempted_writes);
  else assert(rc==ZizhanAmpTimeout);
 }
 /* Every actual run transaction,including stop and both chips:fault must
  * retain ownership and forbid all subsequent cleanup when uncertain. */
 for(unsigned fail=1;fail<=commands;++fail){
  unsigned before=prepare_run(&f,&coeff,coeff_hash,fw_hash);f.fail_command=before+fail;
  assert(zizhan_cs35l41_run(1,flags,&f.bus,&clock,&coeff,&out)==ZizhanAmpBus);
  assert(out.transport.controller_uncertain&&out.status==ZizhanAmpFail&&f.commands==before+fail&&out.attempted_writes<=82);
 }
 for(unsigned mismatch=0;mismatch<2;++mismatch){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_bad_mpu=mismatch;
  assert(!zizhan_cs35l41_mpu_capture(1,flags,&f.bus,&clock,&coeff,&out)&&out.status==ZizhanAmpPass&&out.mpu_capture_only&&out.completed==2);
  assert(out.attempted_writes==78&&out.acknowledged_writes==78&&!f.run_core_writes);
  for(unsigned c=0;c<2;++c){assert(out.mpu_readbacks[c]==20&&out.mpu_mismatches[c]==(mismatch?20u:0u)&&!out.started[c]);
   for(unsigned i=0;i<20;++i)assert(out.mpu_values[c][i]==(0xffffffffu^mismatch));}
 }
 unsigned observed_commands=0;
 for(unsigned test=0;test<24;++test){
  unsigned before=prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_observed=1;
  if(test>=1&&test<=20)f.run_bad_index=test;
  if(test==21)f.run_profile_change=1;
  if(test==22)f.run_freeze=1;
  int rc=test==23?zizhan_cs35l41_run(1,flags,&f.bus,&clock,&coeff,&out):zizhan_cs35l41_observed_run(1,flags,&f.bus,&clock,&coeff,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.observed_mpu_profile);
   assert(out.mpu_stable_readbacks[0]==20&&out.mpu_stable_readbacks[1]==20);observed_commands=f.commands-before;
  }else if(test==22)assert(rc==ZizhanAmpTimeout&&out.stopped[0]);
  else assert(rc==ZizhanAmpReadback&&!out.started[0]);
 }
 for(unsigned fail=1;fail<=observed_commands;++fail){
  unsigned before=prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_observed=1;f.fail_command=before+fail;
  assert(zizhan_cs35l41_observed_run(1,flags,&f.bus,&clock,&coeff,&out)==ZizhanAmpBus);
  assert(out.transport.controller_uncertain&&f.commands==before+fail);
 }
 unsigned clocked_commands=0;
 for(unsigned test=0;test<10;++test){
  unsigned before=prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_observed=1;f.run_pll[0]=f.run_pll[1]=0x10;
  if(test>=1&&test<=6)f.run_bad_pll=test;
  if(test==7){f.run_freeze=1;}if(test==8)f.run_halo_state=1;if(test==9)f.clock_step=5000000;
  int rc=zizhan_cs35l41_clocked_run(1,flags,&f.bus,&clock,&coeff,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.clocked_profile);
   assert(out.attempted_writes==94&&out.acknowledged_writes==94);
   for(unsigned c=0;c<2;++c)assert(out.pll_steps[c]==6&&out.pll_before[c]==0x10&&out.pll_current[c]==0x670&&out.samples[c]==100&&out.heartbeat_changes[c]==99&&out.stopped[c]&&f.state[c][2]==0x100);
   clocked_commands=f.commands-before;
  }else if(test<=6)assert(rc==ZizhanAmpReadback&&out.failed_register==0x2c04&&!out.started[0]&&out.pll_steps[0]==test-1);
  else{assert(rc==ZizhanAmpTimeout);if(test==7||test==8)assert(out.stopped[0]);}
 }
 for(unsigned fail=1;fail<=clocked_commands;++fail){
  if(fail>65&&fail<clocked_commands-4&&fail%137)continue;
  unsigned before=prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_observed=1;f.run_pll[0]=f.run_pll[1]=0x10;f.fail_command=before+fail;
  assert(zizhan_cs35l41_clocked_run(1,flags,&f.bus,&clock,&coeff,&out)==ZizhanAmpBus);
  assert(out.transport.controller_uncertain&&f.commands==before+fail);
 }
 for(unsigned locked=0;locked<2;++locked){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;f.run_lock=locked;
  assert(!zizhan_cs35l41_pll_capture(1,flags,&f.bus,&clock,&coeff,&out)&&out.status==ZizhanAmpPass&&out.pll_capture&&out.completed==2);
  assert(out.attempted_writes==12&&out.acknowledged_writes==12&&!f.run_core_writes&&!f.run_mpu_writes&&!f.run_rate_writes);
  for(unsigned c=0;c<2;++c)assert(out.pll_samples[c]==64&&out.pll_lock_samples[c]==(locked?64u:0u)&&out.pll_raw_or[c][2]==(locked?2u:0u)&&!out.started[c]);
 }
 for(unsigned test=0;test<2;++test){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;f.run_bad_monitor=test;
  int rc=zizhan_cs35l41_clock_monitor_capture(1,flags,&f.bus,&clock,&coeff,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==14&&out.acknowledged_writes==14);
   for(unsigned c=0;c<2;++c)assert(out.monitor_writes[c]==1&&out.monitor_after[c]==0x24010&&out.pll_steps[c]==6&&out.clock_detect_first[c]==0x1234&&out.clock_detect_last[c]==0x1234&&out.pll_samples[c]==64&&!out.started[c]);
  }else assert(rc==ZizhanAmpReadback&&!out.pll_steps[0]&&!out.started[0]);
 }
 unsigned global_commands=0;
 for(unsigned test=0;test<3;++test){
  unsigned before=prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;f.run_allow_global=1;
  if(test==1){f.run_asp_enabled[0]=1;}if(test==2)f.state[0][1]|=1;
  int rc=zizhan_cs35l41_global_pll_capture(1,flags,&f.bus,&clock,&coeff,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==18&&out.acknowledged_writes==18&&f.run_global_writes==4&&!f.run_core_writes);
   global_commands=f.commands-before;
   for(unsigned c=0;c<2;++c)assert(out.global_on[c]==1&&out.global_off[c]==1&&!out.global_active[c]&&!out.asp_enables[c]&&out.asp_checks[c]>=66&&!f.state[c][0]&&f.state[c][1]==0x3300);
  }else assert(rc==ZizhanAmpStateChanged&&!out.attempted_writes&&!f.run_global_writes);
 }
 for(unsigned fail=1;fail<=global_commands;++fail){
  if(fail>70&&fail<global_commands-8&&fail%101)continue;
  unsigned before=prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;f.run_allow_global=1;f.fail_command=before+fail;
  assert(zizhan_cs35l41_global_pll_capture(1,flags,&f.bus,&clock,&coeff,&out)==ZizhanAmpBus&&out.transport.controller_uncertain&&f.commands==before+fail);
 }
 for(unsigned test=0;test<6;++test){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;f.run_allow_global=1;f.run_lock=1;
  if(test==1){f.run_lock=0;}if(test==2)f.run_bad_mpu=1;if(test==3)f.run_freeze=1;
  if(test==4){f.run_asp_enabled[0]=1;}if(test==5)f.run_halo_state=1;
  int rc=zizhan_cs35l41_global_silent_run(1,flags,&f.bus,&clock,&coeff,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==100&&out.acknowledged_writes==100&&!out.observed_mpu_profile);
   for(unsigned c=0;c<2;++c)assert(out.global_on[c]==1&&out.global_off[c]==1&&!out.global_active[c]&&out.started[c]==1&&out.stopped[c]==1&&out.samples[c]==100&&out.heartbeat_changes[c]==99&&out.pll_lock_samples[c]==64&&!out.asp_enables[c]&&!f.state[c][0]&&f.state[c][1]==0x3300);
  }else if(test==1)assert(rc==ZizhanAmpPrerequisite&&!out.started[0]&&!f.run_core_writes);
  else if(test==2)assert(rc==ZizhanAmpReadback&&!out.started[0]);
  else if(test==4)assert(rc==ZizhanAmpStateChanged&&!out.attempted_writes);
  else assert(rc==ZizhanAmpTimeout&&out.stopped[0]);
 }
 for(unsigned test=0;test<2;++test){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;f.run_allow_global=f.run_lock=f.run_observed=1;
  if(test)f.run_profile_change=1;
  int rc=zizhan_cs35l41_global_mpu_capture(1,flags,&f.bus,&clock,&coeff,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==96&&out.acknowledged_writes==96&&!f.run_core_writes&&out.mpu_capture_only&&!out.observed_mpu_profile);
   for(unsigned c=0;c<2;++c)assert(out.mpu_readbacks[c]==20&&out.mpu_stable_readbacks[c]==20&&out.global_on[c]==1&&out.global_off[c]==1&&!out.global_active[c]&&out.mpu_values[c][0]==3&&!out.started[c]);
  }else assert(rc==ZizhanAmpReadback&&!out.started[0]);
 }
 for(unsigned test=0;test<23;++test){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;f.run_allow_global=f.run_lock=f.run_observed=1;
  if(test>=1&&test<=20)f.run_bad_index=test;
  if(test==21){f.run_profile_change=1;}if(test==22)f.run_freeze=1;
  int rc=zizhan_cs35l41_global_observed_run(1,flags,&f.bus,&clock,&coeff,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==100&&out.observed_mpu_profile);
   for(unsigned c=0;c<2;++c)assert(out.mpu_stable_readbacks[c]==20&&out.samples[c]==100&&out.heartbeat_changes[c]>=10&&out.global_off[c]==1&&!out.global_active[c]&&!out.asp_enables[c]&&out.stopped[c]);
  }else if(test<=21)assert(rc==ZizhanAmpReadback&&!out.started[0]);
  else assert(rc==ZizhanAmpTimeout&&out.stopped[0]);
 }
 for(unsigned test=0;test<2;++test){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;f.run_allow_global=f.run_lock=f.run_observed=f.run_native_frame=1;
  f.run_global_fs[0]=f.run_global_fs[1]=0xa5000003;f.run_bad_frame=test;
  int rc=zizhan_cs35l41_native_frame_run(1,flags,&f.bus,&clock,&coeff,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==104&&out.acknowledged_writes==104);
   for(unsigned c=0;c<2;++c)assert(out.frame_writes[c]==2&&out.lrclk_delay[c]==32&&out.global_fs_after[c]==0xa5000004&&out.samples[c]==100&&out.stopped[c]);
  }else assert(rc==ZizhanAmpReadback&&!out.started[0]&&!out.global_on[0]);
 }
 for(unsigned test=0;test<2;++test){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;f.run_allow_global=f.run_lock=f.run_observed=f.run_native_frame=1;f.run_freeze=test;
  int rc=zizhan_cs35l41_native_debug_run(1,flags,&f.bus,&clock,&coeff,&out);
  assert(out.core_debug&&out.debug_count[0][0]==15&&out.debug_count[0][5]==15&&out.stopped[0]);
  for(unsigned slot=0;slot<6;++slot){assert(out.debug_count[0][slot]==15);
   for(unsigned i=0;i<15;++i)assert(out.debug_values[0][slot][i]==0x40000000u+i+((slot>=1&&slot<=4)?0x10000u:0));}
  if(!test)assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==104);
  else assert(rc==ZizhanAmpTimeout&&out.samples[0]==100&&!out.heartbeat_changes[0]);
 }
 for(unsigned test=0;test<6;++test){
  unsigned before=prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;f.run_allow_global=f.run_lock=f.run_observed=f.run_native_frame=f.run_calibration=1;
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};
  if(test==1){cal.version=0;}if(test==2)cal.resistance[0]=0;if(test==3)cal.resistance[1]=0xffffff;
  if(test==4)f.run_bad_calibration=1;
  int rc=zizhan_cs35l41_native_protection_run(1,flags,&f.bus,&clock,&coeff,test==5?NULL:&cal,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==111&&out.acknowledged_writes==111);
   for(unsigned c=0;c<2;++c)assert(out.prerun_writes[c]==(c?3u:4u)&&out.prerun_readbacks[c]==(c?3u:4u)&&out.calibration_r[c]==cal.resistance[c]&&out.calibration_checksum[c]==cal.resistance[c]+1&&out.calibration_status[c]==1&&out.us_bypass[c]==(c?0u:1u)&&out.samples[c]==100&&out.stopped[c]);
  }else if(test==4)assert(rc==ZizhanAmpReadback&&!out.started[0]&&!out.global_on[0]);
  else assert(!rc&&out.status==ZizhanAmpSkip&&f.commands==before&&!out.attempted_writes);
 }
 for(unsigned test=0;test<18;++test){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;
  f.run_allow_global=f.run_lock=f.run_observed=f.run_native_frame=f.run_calibration=f.run_native_rates=1;
  if(test>=2){f.run_bad_native_rate=test-1;}if(test==1)f.run_freeze=1;
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};
  int rc=zizhan_cs35l41_native_rates_run(1,flags,&f.bus,&clock,&coeff,&cal,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==143&&out.acknowledged_writes==143);
   for(unsigned c=0;c<2;++c){assert(out.native_rate_writes[c]==16&&out.native_rate_readbacks[c]==16&&out.samples[c]==100&&out.stopped[c]);
    for(unsigned i=0;i<16;++i)assert(out.native_rate_before[c][i]==0xa5000001&&out.native_rate_after[c][i]==0xa5000000);}
  }else if(test==1)assert(rc==ZizhanAmpTimeout&&out.started[0]&&out.stopped[0]&&out.samples[0]==100&&!out.heartbeat_changes[0]);
  else assert(rc==ZizhanAmpReadback&&!out.started[0]&&out.native_rate_writes[0]==test-1&&out.native_rate_readbacks[0]==test-2);
 }
 for(unsigned test=0;test<4;++test){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;
  f.run_allow_global=f.run_lock=f.run_observed=f.run_native_frame=f.run_calibration=f.run_native_rates=f.run_rx_profile=1;
  if(test==1){f.run_freeze=1;}if(test>=2)f.run_rx_bad=test-1;
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};
  int rc=zizhan_cs35l41_asp_rx_run(1,flags,&f.bus,&clock,&coeff,&cal,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==147&&out.acknowledged_writes==147&&!f.run_asp_enabled[0]&&!f.run_asp_enabled[1]);
   for(unsigned c=0;c<2;++c)assert(out.asp_rx_on[c]==1&&out.asp_rx_off[c]==1&&!out.asp_rx_active[c]&&out.started[c]&&out.stopped[c]&&out.samples[c]==100);
  }else if(test==1)assert(rc==ZizhanAmpTimeout&&out.started[0]&&out.stopped[0]&&out.asp_rx_on[0]&&!out.asp_rx_off[0]&&out.samples[0]==100);
  else if(test==2)assert(rc==ZizhanAmpStateChanged&&!out.started[0]&&out.asp_rx_active[0]&&!out.asp_rx_on[0]);
  else assert(rc==ZizhanAmpStateChanged&&out.started[0]&&out.stopped[0]&&!out.completed);
 }
 for(unsigned test=0;test<3;++test){
  prepare_run(&f,&coeff,coeff_hash,fw_hash);f.run_pll[0]=f.run_pll[1]=0x10;
  f.run_allow_global=f.run_lock=f.run_observed=f.run_native_frame=f.run_calibration=f.run_native_rates=f.run_rx_profile=f.run_format_profile=1;
  f.run_format_value[0]=f.run_format_value[1]=0x181802a2;
  if(test==1){f.run_format_bad=1;}if(test==2)f.run_freeze=1;
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};
  int rc=zizhan_cs35l41_asp_format_run(1,flags,&f.bus,&clock,&coeff,&cal,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==149&&out.acknowledged_writes==149);
   for(unsigned c=0;c<2;++c)assert(out.asp_format_reads[c]==2&&out.asp_format_before[c]==0x181802a2&&out.asp_format_after[c]==0x201800a2&&out.asp_rx_on[c]&&out.asp_rx_off[c]&&!out.asp_rx_active[c]);
  }else if(test==1)assert(rc==ZizhanAmpReadback&&out.asp_format_reads[0]==1&&!out.started[0]&&!out.global_on[0]&&!out.pll_steps[0]);
  else assert(rc==ZizhanAmpTimeout&&out.started[0]&&out.stopped[0]&&!out.completed);
 }
 for(unsigned test=0;test<9;++test){
  ZizhanAmpSnapshot snapshot;ZizhanAmpTrim trim;ZizhanAmpRam ram;prepare(&f,&snapshot,&trim);f.native_core_init=1;
  unsigned before=f.commands;if(test==1)f.native_core_bad=1;if(test==2)f.fail_command=before+4;if(test==3)f.state[0][1]|=1;
  int rc=zizhan_cs35l41_ram_native_init(test==4?0:1,flags,&f.bus,&clock,&snapshot,&trim,firmware,sizeof firmware,fw_hash,&ram);
  if(test==4){assert(!rc&&ram.status==ZizhanAmpSkip&&f.commands==before);continue;}
  if(test==1){assert(rc==ZizhanAmpReadback&&ram.core_init_acknowledged==1&&!ram.attempted_writes&&!ram.completed);continue;}
  if(test==2){assert(rc==ZizhanAmpBus&&ram.transport.controller_uncertain&&!ram.attempted_writes&&!ram.completed);continue;}
  if(test==3){assert(rc==ZizhanAmpStateChanged&&!ram.core_init_attempted&&!ram.attempted_writes);continue;}
  assert(!rc&&ram.status==ZizhanAmpPass&&ram.native_core_init&&ram.core_init_attempted==2&&ram.core_init_acknowledged==2&&ram.attempted_writes==14938&&ram.acknowledged_writes==14938);
  for(unsigned c=0;c<2;++c)assert(ram.core_init_reads[c]==6&&ram.core_init_before[c][2]==0x100&&ram.core_init_after[c][2]==0&&trim.after_state[c][2]==0x100&&ram.after_state[c][2]==0);
  f.coeff_mode=1;coeff_bad_hash=0;
  assert(!zizhan_cs35l41_coefficients(1,flags,&f.bus,&clock,&ram,top_coeff,sizeof top_coeff,bottom_coeff,sizeof bottom_coeff,coeff_hash,&coeff)&&coeff.status==ZizhanAmpPass);
  f.coeff_mode=1;f.run_mode=1;f.run_pll[0]=f.run_pll[1]=0x10;
  f.run_allow_global=f.run_lock=f.run_native_frame=f.run_calibration=f.run_native_rates=f.run_rx_profile=f.run_format_profile=1;
  f.run_format_value[0]=f.run_format_value[1]=0x181802a2;
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};
  if(test>=5){f.run_observed=1;if(test==6)f.run_freeze=1;if(test==8)f.run_bad_index=1;
   const unsigned before_run=f.commands;if(test==7)coeff.after_state[0][2]=0x100;
   int run_rc=zizhan_cs35l41_native_core_run(1,flags,&f.bus,&clock,&coeff,&cal,&out);
   if(test==5){assert(!run_rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==149&&out.acknowledged_writes==149);
    for(unsigned c=0;c<2;++c)assert(out.core_before[c]==0&&out.core_running[c]==1&&out.core_after[c]==0&&out.samples[c]==100&&out.heartbeat_changes[c]>=10&&out.stopped[c]&&out.asp_rx_off[c]&&out.global_off[c]);}
   else if(test==6)assert(run_rc==ZizhanAmpTimeout&&out.started[0]&&out.stopped[0]&&!out.completed&&!out.heartbeat_changes[0]);
   else if(test==7)assert(!run_rc&&out.status==ZizhanAmpSkip&&f.commands==before_run);
   else assert(run_rc==ZizhanAmpReadback&&!out.started[0]&&out.mpu_readbacks[0]==1);
  }else{
   assert(!zizhan_cs35l41_native_core_capture(1,flags,&f.bus,&clock,&coeff,&cal,&out)&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted_writes==145&&!f.run_core_writes);
   for(unsigned c=0;c<2;++c)assert(out.mpu_readbacks[c]==20&&out.mpu_stable_readbacks[c]==20&&!out.started[c]&&!out.samples[c]&&out.asp_rx_off[c]&&!out.global_active[c]&&out.native_rate_readbacks[c]==16&&out.core_before[c]==0);
  }
 }
 for(unsigned test=0;test<7;++test){
  ZizhanAmpSnapshot snapshot;ZizhanAmpTrim trim;ZizhanAmpRam ram;prepare(&f,&snapshot,&trim);f.native_core_init=1;
  assert(!zizhan_cs35l41_ram_native_init(1,flags,&f.bus,&clock,&snapshot,&trim,firmware,sizeof firmware,fw_hash,&ram));
  f.coeff_mode=1;coeff_bad_hash=0;assert(!zizhan_cs35l41_coefficients(1,flags,&f.bus,&clock,&ram,top_coeff,sizeof top_coeff,bottom_coeff,sizeof bottom_coeff,coeff_hash,&coeff));
  f.run_mode=1;f.run_pll[0]=f.run_pll[1]=0x10;
  f.run_allow_global=f.run_lock=f.run_native_frame=f.run_calibration=f.run_native_rates=f.run_rx_profile=f.run_format_profile=f.run_observed=1;
  f.run_format_value[0]=f.run_format_value[1]=0x181802a2;
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};TransferContext context={&f,&out,0,test};ZizhanAudioTransfer transfer={&context,transfer_zero,0};
  unsigned before=f.commands;int rc=zizhan_cs35l41_native_core_transfer(1,flags,&f.bus,&clock,&coeff,&cal,test==3?0:&transfer,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&context.calls==2&&out.attempted_writes==149);
   for(unsigned c=0;c<2;++c)assert(out.transfer_attempts[c]==1&&out.transfer_acks[c]==1&&out.transfer_halo[c][0]==2&&out.transfer_halo[c][1]==2&&out.transfer_heartbeat[c][1]!=out.transfer_heartbeat[c][0]&&out.samples[c]==100&&out.stopped[c]);}
  else if(test==3)assert(!rc&&out.status==ZizhanAmpSkip&&context.calls==0&&f.commands==before);
  else{assert(out.status==ZizhanAmpFail&&out.stopped[test==2?1:0]&&context.calls==(test==2?2u:1u));
   if(test<=2)assert(rc==ZizhanAmpProtocol&&out.completed==(test==2?1u:0u)&&out.transfer_acks[test==2?1:0]==0);
   else if(test==4)assert(rc==ZizhanAmpStateChanged);else assert(rc==ZizhanAmpTimeout);}
 }
 /* Native actual-core mailbox handshake, stale IRQ/wrong state/timeout,
  * exact saved mask, and each of24 write-transport failure controls. */
 unsigned mailbox_fail_commands[24]={0};
 for(unsigned test=0;test<31;++test){
  ZizhanAmpSnapshot snapshot;ZizhanAmpTrim trim;ZizhanAmpRam ram;prepare(&f,&snapshot,&trim);f.native_core_init=1;
  assert(!zizhan_cs35l41_ram_native_init(1,flags,&f.bus,&clock,&snapshot,&trim,firmware,sizeof firmware,fw_hash,&ram));
  f.coeff_mode=1;coeff_bad_hash=0;assert(!zizhan_cs35l41_coefficients(1,flags,&f.bus,&clock,&ram,top_coeff,sizeof top_coeff,bottom_coeff,sizeof bottom_coeff,coeff_hash,&coeff));
  f.run_mode=1;f.run_pll[0]=f.run_pll[1]=0x10;
  f.run_allow_global=f.run_lock=f.run_native_frame=f.run_calibration=f.run_native_rates=f.run_rx_profile=f.run_format_profile=f.run_observed=1;
  f.run_format_value[0]=f.run_format_value[1]=0x181802a2;
  f.mailbox_test=1;f.mailbox_mask[0]=0xa5ffffff;f.mailbox_mask[1]=0x5a0fffff;
  if(test>=1&&test<=4){f.mailbox_fault=test;if(test==3)f.mailbox_irq[0]=0x200000;}
  if(test>=7)f.fail_command=mailbox_fail_commands[test-7];
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};TransferContext context={&f,&out,0,0};ZizhanAudioTransfer transfer={&context,transfer_zero,0};
  unsigned before=f.commands;if(test==5)coeff.after_state[0][2]=0x100;
  int rc=zizhan_cs35l41_native_mailbox_transfer(1,flags,&f.bus,&clock,&coeff,&cal,test==6?0:&transfer,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&context.calls==2&&out.attempted_writes==149);
   for(unsigned c=0;c<2;++c){assert(out.mailbox_completed[c]==2&&out.mailbox_attempted[c]==12&&out.mailbox_acknowledged[c]==12);
    for(unsigned i=0;i<2;++i)assert(out.mailbox_status[c][i]==(i?0u:1u)&&out.mailbox_irq[c][i]&0x200000&&out.mailbox_polls[c][i]==1&&out.mailbox_mask_restored[c][i]&&out.mailbox_mask_after[c][i]==out.mailbox_mask_before[c][i]);
    assert(f.mailbox_count[c]==12);for(unsigned i=0;i<12;++i)mailbox_fail_commands[c*12+i]=f.mailbox_commands[c][i];}
  }else if(test>=7)assert(rc==ZizhanAmpBus&&out.transport.controller_uncertain&&f.commands==f.fail_command);
  else if(test>=5)assert(!rc&&out.status==ZizhanAmpSkip&&f.commands==before&&!context.calls);
  else{assert(rc&&out.status==ZizhanAmpFail&&!context.calls&&out.stopped[0]);
   if(test==1)assert(rc==ZizhanAmpTimeout&&out.mailbox_polls[0][0]==5&&out.mailbox_mask_restored[0][0]);
   if(test==2)assert(rc==ZizhanAmpProtocol&&out.mailbox_mask_restored[0][0]);
   if(test==3)assert(rc==ZizhanAmpStateChanged&&!out.mailbox_completed[0]);
   if(test==4)assert(rc==ZizhanAmpReadback);}
 }
 for(unsigned test=0;test<5;++test){
  ZizhanAmpSnapshot snapshot;ZizhanAmpTrim trim;ZizhanAmpRam ram;prepare(&f,&snapshot,&trim);f.native_core_init=1;
  assert(!zizhan_cs35l41_ram_native_init(1,flags,&f.bus,&clock,&snapshot,&trim,firmware,sizeof firmware,fw_hash,&ram));
  f.coeff_mode=1;coeff_bad_hash=0;assert(!zizhan_cs35l41_coefficients(1,flags,&f.bus,&clock,&ram,top_coeff,sizeof top_coeff,bottom_coeff,sizeof bottom_coeff,coeff_hash,&coeff));
  f.run_mode=1;f.run_pll[0]=f.run_pll[1]=0x10;
  f.run_allow_global=f.run_lock=f.run_native_frame=f.run_calibration=f.run_native_rates=f.run_rx_profile=f.run_format_profile=f.run_observed=1;
  f.run_format_value[0]=f.run_format_value[1]=0x181802a2;f.mailbox_test=1;f.mailbox_mask[0]=f.mailbox_mask[1]=0xffffffff;
  if(test==1||test==2){f.iv_bad=test;}if(test==3)f.state[0][1]&=~0x1000u;
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};TransferContext context={&f,&out,0,test==4?7u:0u};ZizhanAudioTransfer transfer={&context,transfer_zero,0};
  int rc=zizhan_cs35l41_native_iv_transfer(1,flags,&f.bus,&clock,&coeff,&cal,&transfer,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&context.calls==2&&out.attempted_writes==149&&f.iv_reads==8);
   for(unsigned c=0;c<2;++c)for(unsigned i=0;i<2;++i)assert(out.iv_reads[c][i]==3&&out.iv_values[c][i][0]==0x3300&&out.iv_values[c][i][1]==0x18&&out.iv_values[c][i][2]==0x19&&out.mailbox_completed[c]==2);
  }else{assert(rc==ZizhanAmpStateChanged&&out.status==ZizhanAmpFail&&!out.completed);
   if(test==4)assert(context.calls==1&&out.transfer_acks[0]==1&&out.iv_reads[0][1]==3&&out.stopped[0]);
   else assert(!context.calls&&!out.transfer_attempts[0]);}
 }
 unsigned pulse_commands[32]={0};
 for(unsigned test=0;test<36;++test){
  ZizhanAmpSnapshot snapshot;ZizhanAmpTrim trim;ZizhanAmpRam ram;prepare(&f,&snapshot,&trim);f.native_core_init=1;
  assert(!zizhan_cs35l41_ram_native_init(1,flags,&f.bus,&clock,&snapshot,&trim,firmware,sizeof firmware,fw_hash,&ram));
  f.coeff_mode=1;coeff_bad_hash=0;assert(!zizhan_cs35l41_coefficients(1,flags,&f.bus,&clock,&ram,top_coeff,sizeof top_coeff,bottom_coeff,sizeof bottom_coeff,coeff_hash,&coeff));
  f.run_mode=1;f.run_pll[0]=f.run_pll[1]=0x10;
  f.run_allow_global=f.run_lock=f.run_native_frame=f.run_calibration=f.run_native_rates=f.run_rx_profile=f.run_format_profile=f.run_observed=1;
  f.run_format_value[0]=f.run_format_value[1]=0x181802a2;f.mailbox_test=1;f.mailbox_mask[0]=f.mailbox_mask[1]=0xffffffff;
  f.pulse_test=1;f.pulse_volume[0]=f.pulse_volume[1]=0x267c;if(test>=4)f.fail_command=pulse_commands[test-4];
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};PulseContext context={&f,0,test};ZizhanAudioTransfer transfer={&context,transfer_pulse,0};
  int rc=zizhan_cs35l41_native_pulse_transfer(1,flags,&f.bus,&clock,&coeff,&cal,&transfer,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&context.calls==4&&out.attempted_writes==149&&f.pulse_writes==32);
   for(unsigned c=0;c<2;++c){assert(out.pulse[c].complete&&out.pulse[c].patch_on&&out.pulse[c].patch_off&&out.pulse[c].enabled&&out.pulse[c].disabled&&out.pulse[c].data_ack&&out.pulse[c].acknowledged==16&&out.pulse[c].attempted==16&&out.pulse[c].enabled_us<250000&&out.stopped[c]&&f.pulse_volume[c]==0x267c&&f.pulse_patch[c]==0x2f1aa3);}
   memcpy(pulse_commands,f.pulse_commands,sizeof pulse_commands);
  }else if(test>=4)assert(rc&&out.status==ZizhanAmpFail&&out.transport.controller_uncertain&&f.commands==f.fail_command);
  else{assert(rc&&out.status==ZizhanAmpFail&&!out.completed&&out.stopped[0]&&f.state[0][1]==0x3300);
   if(test==1)assert(context.calls==1&&!out.pulse[0].enabled);else assert(context.calls==2&&out.pulse[0].disabled&&!out.pulse[0].complete);}
 }
 unsigned play_total=0;
 for(unsigned test=0;test<7;++test){
  ZizhanAmpSnapshot snapshot;ZizhanAmpTrim trim;ZizhanAmpRam ram;prepare(&f,&snapshot,&trim);f.native_core_init=1;
  assert(!zizhan_cs35l41_ram_native_init(1,flags,&f.bus,&clock,&snapshot,&trim,firmware,sizeof firmware,fw_hash,&ram));
  f.coeff_mode=1;coeff_bad_hash=0;assert(!zizhan_cs35l41_coefficients(1,flags,&f.bus,&clock,&ram,top_coeff,sizeof top_coeff,bottom_coeff,sizeof bottom_coeff,coeff_hash,&coeff));
  f.run_mode=1;f.run_pll[0]=f.run_pll[1]=0x10;
  f.run_allow_global=f.run_lock=f.run_native_frame=f.run_calibration=f.run_native_rates=f.run_rx_profile=f.run_format_profile=f.run_observed=1;
  f.run_format_value[0]=f.run_format_value[1]=0x181802a2;f.mailbox_test=1;f.mailbox_mask[0]=f.mailbox_mask[1]=0xffffffff;
  f.pulse_test=1;f.pulse_volume[0]=f.pulse_volume[1]=0x267c;f.play_fault=0;f.run_halo_state=0;
  zizhan_amp_pulse_flags=test==1?1u:0u;zizhan_amp_play_ms=test==6?0u:500u;
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};PlayContext context={&f,0,0,test,test==1?1u:0u};ZizhanAudioTransfer transfer={&context,0,transfer_play};
  const unsigned before=f.commands;
  int rc=zizhan_cs35l41_native_play_transfer(1,flags,&f.bus,&clock,&coeff,&cal,&transfer,&out);
  if(test==6){assert(!rc&&out.reason==ZizhanAmpPrerequisite&&f.commands==before&&!context.calls);continue;}
  assert(context.calls==1&&out.play_profile&&out.play_ready[0]&&out.play_ready[1]);
  for(unsigned c=0;c<2;++c){assert(out.pulse[c].armed&&out.pulse[c].disabled&&out.stopped[c]&&out.play_torn[c]&&f.state[c][1]==0x3300&&f.state[c][2]==out.core_before[c]&&f.pulse_volume[c]==0x267c&&!out.asp_rx_active[c]&&!out.global_active[c]);}
  if(test<=1){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.play_streamed&&context.polls==50&&out.play_polls==50);
   for(unsigned c=0;c<2;++c)assert(out.pulse[c].complete&&out.pulse[c].patch_off&&out.pulse[c].enabled==(test?0u:1u)&&out.pulse[c].polls==50);
   if(!test)play_total=f.commands-before;}
  else{assert(rc&&out.status==ZizhanAmpFail&&out.play_streamed==(test==4?1u:0u));
   for(unsigned c=0;c<2;++c)assert(!out.pulse[c].complete&&!out.pulse[c].patch_off&&out.pulse[c].enabled);
   if(test==2)assert(rc==ZizhanAmpProtocol&&context.polls==21);
   if(test==3)assert(rc==ZizhanAmpStateChanged&&context.polls==31);
   if(test==4)assert(rc==ZizhanAmpProtocol&&context.polls==11);  /* stream ignored the abort: still a failure */
   if(test==5)assert(rc==ZizhanAmpProtocol&&context.polls==50);}
 }
 zizhan_amp_pulse_flags=0;zizhan_amp_play_ms=500;
 /* Every bus command of the window failing: the bus becomes uncertain and nothing more is sent. */
 for(unsigned fail=play_total-260;fail<=play_total;fail+=13){
  ZizhanAmpSnapshot snapshot;ZizhanAmpTrim trim;ZizhanAmpRam ram;prepare(&f,&snapshot,&trim);f.native_core_init=1;
  assert(!zizhan_cs35l41_ram_native_init(1,flags,&f.bus,&clock,&snapshot,&trim,firmware,sizeof firmware,fw_hash,&ram));
  f.coeff_mode=1;coeff_bad_hash=0;assert(!zizhan_cs35l41_coefficients(1,flags,&f.bus,&clock,&ram,top_coeff,sizeof top_coeff,bottom_coeff,sizeof bottom_coeff,coeff_hash,&coeff));
  f.run_mode=1;f.run_pll[0]=f.run_pll[1]=0x10;
  f.run_allow_global=f.run_lock=f.run_native_frame=f.run_calibration=f.run_native_rates=f.run_rx_profile=f.run_format_profile=f.run_observed=1;
  f.run_format_value[0]=f.run_format_value[1]=0x181802a2;f.mailbox_test=1;f.mailbox_mask[0]=f.mailbox_mask[1]=0xffffffff;
  f.pulse_test=1;f.pulse_volume[0]=f.pulse_volume[1]=0x267c;f.play_fault=0;f.run_halo_state=0;
  const unsigned before=f.commands;f.fail_command=before+fail;
  ZizhanAmpSpeakerCalibration cal={1,sizeof cal,{12001,13001}};PlayContext context={&f,0,0,0,0};ZizhanAudioTransfer transfer={&context,0,transfer_play};
  int rc=zizhan_cs35l41_native_play_transfer(1,flags,&f.bus,&clock,&coeff,&cal,&transfer,&out);
  assert(rc&&out.status==ZizhanAmpFail&&out.transport.controller_uncertain&&f.commands==f.fail_command);
  f.fail_command=0;
 }
 puts("PASS native protection-fw9+playwindow(both amps held,dry,fault/HALO abort,stream fail,refuse,late bus failures) volatilecachedR/checksum/status/topbypass,strict input/reads/ordering/no ambient ormeasurement,HALOguards/native rateblock low5zero/highbitspreserved/all16badreadbacks/HBfailure;no hardware");return 0;
}
