/* Full production GENI/init/RAM/coeff chain;synthetic or private original files. */
#define ZIZHAN_RAM_NO_MAIN
#include "cs35l41-ram.c"
#undef ZIZHAN_RAM_NO_MAIN
static uint8_t top_coeff[5060],bottom_coeff[5148];
static int coeff_bad_hash;
static int coeff_mock_hash(const uint8_t *p,unsigned length,uint8_t digest[32]){
 const char *hex=p==top_coeff?"d6da7b1d13f986170c4258ac31f73b381473e354315182fae0a6bfdd297e9835":"cd4a09de82c2d0878e99de7e28aa1a111668d7a12bffef6779c64b9f2937911b";
 assert((p==top_coeff&&length==5060)||(p==bottom_coeff&&length==5148));
 for(unsigned i=0;i<32;++i){unsigned v=0;assert(sscanf(hex+2*i,"%2x",&v)==1);digest[i]=(uint8_t)v;}
 if(coeff_bad_hash&&p==bottom_coeff){digest[0]^=1;}return 0;
}
static unsigned prepare_coeff(Fake *f,ZizhanAmpRam *ram,ZizhanAmpHash fw_hash){
 ZizhanAmpSnapshot snapshot;ZizhanAmpTrim trim;prepare(f,&snapshot,&trim);
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 const ZizhanAmpClock clock={f,now,pause_us};
 assert(!zizhan_cs35l41_ram_burst(1,flags,&f->bus,&clock,&snapshot,&trim,firmware,sizeof firmware,fw_hash,ram)&&ram->status==ZizhanAmpPass);
 f->coeff_mode=1;coeff_bad_hash=0;return f->commands;
}
static void read_file(const char *path,uint8_t *to,unsigned length){FILE *file=fopen(path,"rb");assert(file);assert(fread(to,1,length,file)==length&&fgetc(file)==EOF);fclose(file);}
#ifndef ZIZHAN_COEFF_NO_MAIN
int main(int argc,char **argv){
 static Fake f;ZizhanAmpRam ram;ZizhanAmpCoefficients out;
 ZizhanAmpHash fw_hash=synthetic_hash,coeff_hash=coeff_mock_hash;
 for(unsigned i=0;i<sizeof firmware;++i)firmware[i]=(uint8_t)(i*37u+19u);
 for(unsigned i=0;i<sizeof top_coeff;++i)top_coeff[i]=(uint8_t)(i*17+3);
 for(unsigned i=0;i<sizeof bottom_coeff;++i)bottom_coeff[i]=(uint8_t)(i*29+7);
 if(argc==4){read_file(argv[1],firmware,sizeof firmware);read_file(argv[2],top_coeff,sizeof top_coeff);read_file(argv[3],bottom_coeff,sizeof bottom_coeff);fw_hash=coeff_hash=actual_hash;}
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 const ZizhanAmpClock clock={&f,now,pause_us};unsigned transactions=0;
 for(unsigned test=0;test<10;++test){
  const unsigned before=prepare_coeff(&f,&ram,fw_hash),writes=f.ram_value_writes;
  unsigned enabled=1,top_size=sizeof top_coeff;if(test==1)enabled=0;if(test==2)ram.readbacks[1]=7468;
  if(test==3){top_size--;}if(test==4)coeff_bad_hash=1;if(test==5)f.identity_bad=1;
  if(test==6){f.ram_mismatch=1;}if(test==7)f.state[0][0]=1;
  if(test==8){f.stall_command=before+48,f.frozen=1;}if(test==9)f.clock_step=10000000;
  int rc=zizhan_cs35l41_coefficients(enabled,flags,&f.bus,&clock,&ram,top_coeff,top_size,bottom_coeff,sizeof bottom_coeff,
      test==4?coeff_mock_hash:coeff_hash,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.source_verified==2);
   assert(out.attempted_writes==1883&&out.acknowledged_writes==1883&&f.ram_value_writes-writes==1883);
   assert(out.identity_words[0]==22&&out.identity_words[1]==22&&out.readbacks[0]==933&&out.readbacks[1]==950);
   assert(out.segments_verified[0]==1&&out.segments_verified[1]==2&&out.state_count[0]==12&&out.state_count[1]==18);
   transactions=f.commands-before;
  }else if(test==1||test==2)assert(!rc&&out.status==ZizhanAmpSkip&&f.commands==before);
  else if(test==3||test==4)assert(rc&&out.status==ZizhanAmpFail&&f.commands==before);
  else if(test==5)assert(rc==ZizhanAmpWrongIdentity&&!out.attempted_writes&&out.identity_words[0]==22);
  else if(test==6)assert(rc==ZizhanAmpReadback);
  else if(test==7)assert(rc==ZizhanAmpStateChanged&&!out.attempted_writes);
  else assert(rc==ZizhanAmpTimeout);
 }
 for(unsigned fail=1;fail<=transactions;++fail){
  if(fail>3&&fail<transactions-3&&fail%31)continue;
  const unsigned before=prepare_coeff(&f,&ram,fw_hash);f.fail_command=before+fail;
  assert(zizhan_cs35l41_coefficients(1,flags,&f.bus,&clock,&ram,top_coeff,sizeof top_coeff,bottom_coeff,sizeof bottom_coeff,coeff_hash,&out)==ZizhanAmpBus);
  assert(out.transport.controller_uncertain&&f.commands==before+fail&&out.attempted_writes<=1883);
 }
 puts("PASS production per-chip coefficient fullchain,actual algorithm/base guards,exact spans/raw readback,both identities before writes,wronghash/partial/fault/timeout;no hardware");return 0;
}
#endif
