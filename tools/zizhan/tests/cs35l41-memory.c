/* Actual ADSP lease protocol with synthetic replies,no device or buffers. */
#include <assert.h>
#include <stdio.h>
#include "ZizhanAudioMemory.h"
static void put(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static unsigned reply(ZizhanMemoryState *s,uint8_t *out,uint32_t op,uint32_t first,uint32_t second){
 GprPacket p={0};p.src_domain=2;p.dst_domain=3;p.src_port=1;p.dst_port=ZizhanMemoryPort;p.token=s->token;p.opcode=op;
 uint8_t payload[8];put(payload,first);put(payload+4,second);return gpr_encode(out,512,&p,payload,op==AR_OP_MEM_MAP_DONE?4:8);
}
int main(void){
 uint8_t tx[512],rx[512];GprPacket p;
 for(unsigned test=0;test<8;++test){ZizhanMemoryState s={0};s.address=0x1f0000000ull;
  if(test==1){s.address=0;}if(test==2)s.address++;
  unsigned n=zizhan_memory_next(&s,100,tx);
  if(test==1||test==2){assert(!n&&s.phase==ZizhanMemoryFailed&&!s.sent);continue;}
  assert(n==44&&!gpr_decode(tx,n,&p)&&p.src_port==ZizhanMemoryPort&&p.dst_port==1&&p.opcode==AR_OP_MEM_MAP);
  assert(zizhan_tdm_le32(tx+24)==0x10003&&zizhan_tdm_le32(tx+28)==0&&zizhan_tdm_le32(tx+32)==0xf0000000&&zizhan_tdm_le32(tx+36)==1&&zizhan_tdm_le32(tx+40)==65536);
  unsigned len=reply(&s,rx,AR_OP_MEM_MAP_DONE,test==3?0:0x567,0);
  if(test==4){rx[16]^=1;assert(!zizhan_memory_receive(&s,rx,len));zizhan_memory_tick(&s,2000100);assert(s.phase==ZizhanMemoryFailed&&s.uncertain);continue;}
  if(test==5){len=reply(&s,rx,GPR_OP_BASIC_RESULT,AR_OP_MEM_MAP,0);assert(!zizhan_memory_receive(&s,rx,len)&&!s.handle&&s.waiting);zizhan_memory_tick(&s,2000100);assert(s.phase==ZizhanMemoryFailed);continue;}
  if(test==3){assert(zizhan_memory_receive(&s,rx,len)==-1&&s.uncertain);continue;}
  assert(zizhan_memory_receive(&s,rx,len)==1&&s.phase==ZizhanMemoryHeld&&s.handle==0x567&&!s.uncertain&&s.ack==1);
  s.phase=ZizhanMemoryUnmap;n=zizhan_memory_next(&s,200,tx);assert(n==28&&zizhan_tdm_le32(tx+24)==0x567);
  len=reply(&s,rx,GPR_OP_BASIC_RESULT,AR_OP_MEM_UNMAP,test==6?5:0);
  if(test==6){assert(zizhan_memory_receive(&s,rx,len)==-1&&s.uncertain);continue;}
  if(test==7){zizhan_memory_tick(&s,2000200);assert(s.phase==ZizhanMemoryFailed&&s.uncertain);continue;}
  assert(zizhan_memory_receive(&s,rx,len)==1&&s.phase==ZizhanMemoryDone&&s.sent==2&&s.ack==2&&!s.uncertain);
 }
 puts("PASS exactnative ADSP map/unmap lease/actualhandle required/strictpeer-token-status/negative retention;no data/hardware");
}
