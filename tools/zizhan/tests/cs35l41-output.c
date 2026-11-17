/* Actual output-disabled configuration core under the production GENI model. */
#define ZIZHAN_COEFF_NO_MAIN
#include "cs35l41-coefficients.c"
#undef ZIZHAN_COEFF_NO_MAIN
static ZizhanAmpHash output_fw_hash=synthetic_hash,output_coeff_hash=coeff_mock_hash;
static void prepare_output(Fake *f,ZizhanAmpCoefficients *coeff){
 ZizhanAmpSnapshot snapshot;ZizhanAmpTrim trim;ZizhanAmpRam ram;prepare(f,&snapshot,&trim);f->native_core_init=1;
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 const ZizhanAmpClock clock={f,now,pause_us};
 assert(!zizhan_cs35l41_ram_native_init(1,flags,&f->bus,&clock,&snapshot,&trim,firmware,sizeof firmware,output_fw_hash,&ram));
 f->coeff_mode=1;coeff_bad_hash=0;assert(!zizhan_cs35l41_coefficients(1,flags,&f->bus,&clock,&ram,top_coeff,sizeof top_coeff,bottom_coeff,sizeof bottom_coeff,output_coeff_hash,coeff));
 f->output_setup=1;for(unsigned c=0;c<2;++c)for(unsigned i=0;i<13;++i)f->output_values[c][i]=0xa5008000u+(c<<20)+(i<<16)+i;
}
int main(int argc,char **argv){static Fake f;ZizhanAmpCoefficients coeff;ZizhanAmpOutputSetup out;(void)prepare_coeff;
 for(unsigned i=0;i<sizeof firmware;++i)firmware[i]=(uint8_t)(i*37u+19u);
 for(unsigned i=0;i<sizeof top_coeff;++i){top_coeff[i]=(uint8_t)(i*17+3);}for(unsigned i=0;i<sizeof bottom_coeff;++i)bottom_coeff[i]=(uint8_t)(i*29+7);
 if(argc==4){read_file(argv[1],firmware,sizeof firmware);read_file(argv[2],top_coeff,sizeof top_coeff);read_file(argv[3],bottom_coeff,sizeof bottom_coeff);output_fw_hash=output_coeff_hash=actual_hash;}
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 const ZizhanAmpClock clock={&f,now,pause_us};unsigned write_at[28]={};
 const uint32_t masks[]={0xffff,0xff03,0x7f,3,3,0x707,0x7f,0x7f,0x7f,0x3e0,0x3ff8,7,1,0xff};
 const uint32_t top[]={0x2424,0x7500,0x40,3,3,0x200,0x32,8,9,0xa0,0x2678,4,1,0};
 const uint32_t bottom[]={0x2424,0x7500,0x40,3,3,0x401,0x32,8,8,0xa0,0x2678,4,1,0};
 for(unsigned test=0;test<9;++test){prepare_output(&f,&coeff);unsigned before=f.commands,enabled=1,verified=flags;
  if(test==1){enabled=0;}if(test==2)verified=0;if(test==3)coeff.readbacks[1]=949;if(test==4)coeff.transport.controller_uncertain=1;
  if(test==5){coeff.after_state[0][2]=0x100;}if(test==6)f.state[1][2]=0x100;if(test==7)f.output_bad=1;if(test==8)f.clock_step=1000000;
  int rc=zizhan_cs35l41_output_setup(enabled,verified,&f.bus,&clock,&coeff,&out);
  if(!test){if(rc||out.status!=ZizhanAmpPass)fprintf(stderr,"output rc=%d status=%u reason=%u chip=%u index=%u attempted=%u ack=%u reads=%u/%u before=%x after=%x time=%llu\n",rc,out.status,out.reason,out.failed_chip,out.failed_index,out.attempted,out.acknowledged,out.readbacks[0],out.readbacks[1],out.before[out.failed_chip][out.failed_index],out.after[out.failed_chip][out.failed_index],(unsigned long long)out.elapsed_us);
   assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.attempted==28&&out.acknowledged==28&&out.readbacks[0]==14&&out.readbacks[1]==14&&f.output_writes==28);
   for(unsigned c=0;c<2;++c){uint32_t expected[13];for(unsigned i=0;i<13;++i)expected[i]=0xa5008000u+(c<<20)+(i<<16)+i;
    for(unsigned i=0;i<14;++i){int slot=output_slot(zizhan_output_fields[i].reg);assert(slot>=0&&zizhan_output_fields[i].mask==masks[i]);uint32_t v=c?bottom[i]:top[i];assert(out.before[c][i]==expected[slot]);expected[slot]=(expected[slot]&~masks[i])|v;assert(out.after[c][i]==expected[slot]);}
    assert(!memcmp(expected,f.output_values[c],sizeof expected)&&f.state[c][0]==0&&f.state[c][1]==0x3300&&f.state[c][2]==0);
   }for(unsigned i=0;i<28;++i)write_at[i]=f.output_commands[i]-before;
  }else if(test<=5)assert(!rc&&out.status==ZizhanAmpSkip&&f.commands==before&&!f.output_writes);
  else if(test==6)assert(rc==ZizhanAmpStateChanged&&out.status==ZizhanAmpFail&&!out.attempted&&!f.output_writes);
  else if(test==7)assert(rc==ZizhanAmpReadback&&out.attempted==1&&out.acknowledged==1&&!out.completed);
  else assert(out.status==ZizhanAmpFail&&out.reason&&out.attempted<28);
 }
 for(unsigned fail=0;fail<28;++fail){prepare_output(&f,&coeff);f.fail_command=f.commands+write_at[fail];int rc=zizhan_cs35l41_output_setup(1,flags,&f.bus,&clock,&coeff,&out);
  assert(rc&&out.status==ZizhanAmpFail&&out.attempted==fail+1&&out.acknowledged==fail&&out.completed==(fail>=14?1u:0u));
  for(unsigned c=0;c<2;++c)assert(f.state[c][0]==0&&f.state[c][1]==0x3300&&f.state[c][2]==0);
 }
 puts("PASS actual native output-off setup/14fields perchip/full masked readback/bothchip proof before writes/all28writefailures/timeout/controller retention/noAMPBSTGLOBALOTPwrites;no hardware");
}
