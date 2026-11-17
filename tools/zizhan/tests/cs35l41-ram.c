/* Synthetic bus execution; optional retained private firmware input is hashed
 * with production SHA256, independently compared with Python hashlib.
 * Mock hashes are used only for synthetic payloads. */
#define main identity_regressions_main
#include "cs35l41-identity.c"
#undef main

#include <stdlib.h>
#include "ZizhanAmpSha256.h"
static uint8_t firmware[ZizhanAmpRamFileBytes];
static int hash_bad,hash_fail;
static int synthetic_hash(const uint8_t *p,unsigned length,uint8_t digest[32]){
 const uint8_t known[]={0xc1,0x1c,0xb3,0xe2,0x58,0x25,0xc6,0x73,0xa9,0x55,0xa3,0x51,0x93,0x70,0xad,0x77,0xfb,0xcd,0x8b,0xcf,0x68,0x80,0x8e,0x8e,0x34,0x14,0x0a,0x70,0xca,0xef,0xce,0xc4};
 assert(p==firmware&&length==sizeof firmware);memcpy(digest,known,32);digest[0]^=hash_bad;return hash_fail;
}
static int actual_hash(const uint8_t *p,unsigned length,uint8_t digest[32]){
 zizhan_amp_sha256(p,length,digest);return 0;
}
static unsigned prepare(Fake *f,ZizhanAmpSnapshot *snapshot,ZizhanAmpTrim *trim){
 ZizhanAmpB2Patch patch;prepare_trim(f,snapshot,&patch);
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 const ZizhanAmpClock clock={f,now,pause_us};
 assert(!zizhan_cs35l41_trim(1,flags,&f->bus,&clock,snapshot,&patch,trim)&&trim->status==ZizhanAmpPass);
 f->allow_ram=1;hash_bad=hash_fail=0;return f->commands;
}
#ifndef ZIZHAN_RAM_NO_MAIN
int main(int argc,char **argv){
 static Fake f;ZizhanAmpSnapshot snap;ZizhanAmpTrim trim;ZizhanAmpRam out;
 const unsigned flags=ZizhanAmpExclusiveSe15|ZizhanAmpPinsVerified|ZizhanAmpResetReleased|ZizhanAmpUnmanagedSupplyTopology;
 const ZizhanAmpClock clock={&f,now,pause_us};
 for(unsigned i=0;i<sizeof firmware;++i)firmware[i]=(uint8_t)(i*37u+19u);
 ZizhanAmpHash hash=synthetic_hash;
 if(argc==2){FILE *in=fopen(argv[1],"rb");assert(in);assert(fread(firmware,1,sizeof firmware,in)==sizeof firmware&&fgetc(in)==EOF);fclose(in);hash=actual_hash;}
 for(unsigned n=0;n<=256;++n){unsigned length=n==256?sizeof firmware:n;uint8_t d[32];assert(!actual_hash(firmware,length,d));
  printf("HASH %u ",length);for(unsigned i=0;i<32;++i)printf("%02x",d[i]);puts("");}
 unsigned transfers=0;
 for(unsigned test=0;test<15;++test){
  unsigned before=prepare(&f,&snap,&trim);unsigned enabled=1,verified=flags,length=sizeof firmware;
  if(test==1)enabled=0;
  if(test==2)verified=0;
  if(test==3)trim.readbacks[1]=97;
  if(test==4)trim.transport.controller_uncertain=1;
  if(test==5)length-=4;
  if(test==6)hash_bad=1;
  if(test==7)hash_fail=1;
  if(test==8)f.ram_mismatch=1;
  if(test==9)f.state[0][0]=1;
  if(test==10)f.override_at=0x614,f.override_value=GENI_M_IRQ_WANTED|0x80;
  if(test==11)f.clock_step=45000000;
  if(test==12)f.stall_command=before+8,f.frozen=1;
  if(test==13)f.bus.phase=GeniTxWait;
  if(test==14)f.global_change_command=before+7500;
  int rc=zizhan_cs35l41_ram(enabled,verified,&f.bus,&clock,&snap,&trim,firmware,length,
      (test==6||test==7)?synthetic_hash:hash,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.source_verified&&out.completed==2);
   assert(out.attempted_writes==14938&&out.acknowledged_writes==14938&&f.ram_value_writes==14938);
   for(unsigned c=0;c<2;++c)assert(out.readbacks[c]==7469&&out.segments_verified[c]==6&&out.segments_written[c]==6&&out.state_count[c]==39);
   assert(f.bus.address==0x55);transfers=f.commands-before;
  }else if(test<=4||test==13)assert(!rc&&out.status==ZizhanAmpSkip&&f.commands==before);
  else if(test<=7)assert(rc&&out.status==ZizhanAmpFail&&f.commands==before);
  else if(test==8)assert(rc==ZizhanAmpReadback&&out.readbacks[0]==1&&!out.completed&&out.acknowledged_writes==7469);
  else if(test==9||test==10||test==14)assert(rc==ZizhanAmpStateChanged);
  else assert(rc==ZizhanAmpTimeout);
 }
 /* Sample every97th transaction plus first/last, exercising partial writes,
  * both chips/readback phases and state checks on the actual GENI core. */
 for(unsigned fail=1;fail<=transfers;++fail){
  if(fail>3&&fail<transfers-3&&fail%97)continue;
  const unsigned before=prepare(&f,&snap,&trim);f.fail_command=before+fail;
  assert(zizhan_cs35l41_ram(1,flags,&f.bus,&clock,&snap,&trim,firmware,sizeof firmware,hash,&out)==ZizhanAmpBus);
  assert(out.transport.controller_uncertain&&out.status==ZizhanAmpFail&&f.commands==before+fail);
  assert(out.attempted_writes<=14938&&out.acknowledged_writes<=out.attempted_writes);
 }
 for(unsigned test=0;test<3;++test){
  const unsigned before=prepare(&f,&snap,&trim);if(test==1)f.ram_mismatch=1;if(test==2)f.fail_command=before+48;
  int rc=zizhan_cs35l41_ram_header(1,flags,&f.bus,&clock,&snap,&trim,firmware,sizeof firmware,hash,&out);
  if(test==2){assert(rc==ZizhanAmpBus&&out.transport.controller_uncertain);continue;}
  assert(!rc&&out.status==ZizhanAmpPass&&out.header_only&&out.completed==1);
  assert(out.attempted_writes==18&&out.acknowledged_writes==18&&out.packed_count==18&&out.unpacked_count==24);
  assert(out.mismatches==(test==1?18u:0u)&&!out.readbacks[1]&&!out.segments_written[1]);
  for(unsigned i=0;i<24;++i)assert(out.header_unpacked[i]==0xabc000+i);
 }
 /* Real diagnostic shape:sparse scalar packed reads but full burst content.
  * Scalar negative control must still fail;burst compares every raw word. */
 unsigned burst_commands=0;
 for(unsigned test=0;test<6;++test){
  const unsigned before=prepare(&f,&snap,&trim);f.ram_sparse=1;
  if(test==2)f.ram_mismatch=1;
  if(test==3)f.short_fifo=1;
  if(test==4)f.stall_command=before+7500,f.frozen=1;
  if(test==5)f.clock_step=45000000;
  int rc=test==1?zizhan_cs35l41_ram(1,flags,&f.bus,&clock,&snap,&trim,firmware,sizeof firmware,hash,&out):
      zizhan_cs35l41_ram_burst(1,flags,&f.bus,&clock,&snap,&trim,firmware,sizeof firmware,hash,&out);
  if(!test){assert(!rc&&out.status==ZizhanAmpPass&&out.completed==2&&out.burst_reads==3618);
   assert(out.readbacks[0]==7469&&out.readbacks[1]==7469&&out.acknowledged_writes==14938);burst_commands=f.commands-before;
  }else if(test==1||test==2)assert(rc==ZizhanAmpReadback);
  else if(test==3)assert(rc==ZizhanAmpBus&&out.transport.bus_error==GeniErrNoData&&out.transport.controller_uncertain);
  else assert(rc==ZizhanAmpTimeout);
 }
 for(unsigned fail=1;fail<=burst_commands;++fail){
  if(fail>3&&fail<burst_commands-3&&fail%97)continue;
  const unsigned before=prepare(&f,&snap,&trim);f.fail_command=before+fail;
  assert(zizhan_cs35l41_ram_burst(1,flags,&f.bus,&clock,&snap,&trim,firmware,sizeof firmware,hash,&out)==ZizhanAmpBus);
  assert(out.transport.controller_uncertain&&out.status==ZizhanAmpFail&&f.commands==before+fail);
 }
 prepare(&f,&snap,&trim);const uint8_t request[]={2,0,0,0};unsigned count=f.commands;
 for(unsigned size=0;size<24;++size)if(size!=12&&size!=20){
  assert(geni_i2c_start_read_block(&f.bus,request,4,size));assert(f.commands==count);
 }
 assert(geni_i2c_start_read_bytes(&f.bus,request,4,12));assert(f.commands==count);
 puts("PASS production RAM full initialization chain,exact payload/readback,guards,bounded failures/header-only mismatch capture;no hardware");return 0;
}
#endif
